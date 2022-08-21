/*
 * Copyright (C) 2020 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Code for communicating shared memory between participants in a
 * multi-participant simulation.
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <memory.h>
#include <usfstl/alloc.h>
#include <usfstl/test.h>
#include <usfstl/rpc.h>
#include <usfstl/multi.h>
#include <usfstl/sharedmem.h>
#include "internal.h"
#include "multi-rpc.h"

#define SECTION_SIZE(s) ((unsigned)(s->p_stop - s->p_start))

#define for_each_msg_section(s, msg_end, msg, msg_size)			\
	for (s = &msg->sections[0], msg_end = (char *)msg + msg_size;	\
	     s->buf <= msg_end && s->buf + s->size <= msg_end;		\
	     s = (void *)(s->buf + s->size))

// make the section exist even in non-multi builds
static const struct usfstl_shared_mem_section * const usfstl_shared_mem_section_NULL
	__attribute__((used, section("usfstl_shms"))) = NULL;

// Shared memory message for receiving/sending from/to a remote participant, or
// the controller.
// The buffer inside the msg stores our notion of the remote participant's view
// of the shared memory
struct usfstl_shared_mem_msg *g_usfstl_shared_mem_msg;
// the size of the message struct
static unsigned int g_usfstl_shared_mem_msg_size;

// indicates that the local view of the shared mem has changed since last sent
// to our parent controller (if any)
bool g_usfstl_shared_mem_dirty;

// return the size of the shared memory message to send to a participant / to
// the controller
unsigned int usfstl_shared_mem_get_msg_size(bool is_participant_outdated)
{
	// include the buffer only if the participant is outdated
	return is_participant_outdated ? g_usfstl_shared_mem_msg_size : 0;
}

// try to find a given section inside the shared memory message
static struct usfstl_shared_mem_msg_section *usfstl_shared_mem_find_msg_section(
	const usfstl_shared_mem_section_name_t name, unsigned int buf_size)
{
	char *msg_end;
	struct usfstl_shared_mem_msg_section *s;

	for_each_msg_section(s, msg_end, g_usfstl_shared_mem_msg,
			     g_usfstl_shared_mem_msg_size) {
		if (strncmp(s->name, name, sizeof(s->name)) == 0) {
			USFSTL_ASSERT_EQ(s->size, buf_size, "%u");
			return s;
		}
	}

	return NULL;
}

// add a section to the shared memory message - it must not already exist
static struct usfstl_shared_mem_msg_section *usfstl_shared_mem_add_msg_section(
	const usfstl_shared_mem_section_name_t name, unsigned int buf_size)
{
	unsigned int new_size;
	struct usfstl_shared_mem_msg_section *section;

	new_size = g_usfstl_shared_mem_msg_size + sizeof(*section) + buf_size;
	g_usfstl_shared_mem_msg = usfstl_realloc(g_usfstl_shared_mem_msg,
						 new_size);
	USFSTL_ASSERT(g_usfstl_shared_mem_msg);

	section = (void *)((char *)g_usfstl_shared_mem_msg->sections +
			   g_usfstl_shared_mem_msg_size);
	memcpy(section->name, name, sizeof(section->name));
	section->size = buf_size;
	g_usfstl_shared_mem_msg_size = new_size;
	return section;
}

// merge a local section into the shared memory message
// return whether the message has changed (so it needs to be sent)
static bool usfstl_shared_mem_merge_local_section(
	struct usfstl_shared_mem_section *s)
{
	const void *buf = s->p_start;
	unsigned int buf_size = SECTION_SIZE(s);
	struct usfstl_shared_mem_msg_section *section;

	// try to find the section in the existing space
	section = usfstl_shared_mem_find_msg_section(s->name, buf_size);

	// if not found, create it
	if (!section)
	{
		section = usfstl_shared_mem_add_msg_section(s->name, buf_size);
		// verify that the section is initially zeroed and consider it unchanged
		memset(section->buf, 0, buf_size);
		USFSTL_ASSERT(memcmp(section->buf, buf, buf_size) == 0,
			"section '%s' initially not zeroed", s->name);
		return false;
	}
	// else, compare it to know whether it changed
	else if (memcmp(section->buf, buf, buf_size) == 0)
		return false;

	memcpy(section->buf, buf, buf_size);
	return true;
}

// merge a remote section into the shared memory message
static void usfstl_shared_mem_merge_remote_section(
	const usfstl_shared_mem_section_name_t name,
	const void *buf,
	unsigned int buf_size)
{
	struct usfstl_shared_mem_msg_section *section;

	// try to find the section in the existing space
	section = usfstl_shared_mem_find_msg_section(name, buf_size);

	if (!section) {
		// if not existing, check if the section is relevant
		// however, the controller keeps all sections
		if (!usfstl_is_multi_controller()) {
			bool relevant = false;
			struct usfstl_shared_mem_section *s;
			int i;

			for_each_shared_mem_section(s, i) {
				if (strncmp(s->name, name,
					    sizeof(s->name)) == 0) {
					USFSTL_ASSERT_EQ(SECTION_SIZE(s),
							 buf_size, "%u");
					relevant = true;
					break;
				}
			}

			if (!relevant)
				return;
		}

		// and then create it
		section = usfstl_shared_mem_add_msg_section(name, buf_size);
	}

	memcpy(section->buf, buf, buf_size);
}

// merge an incoming message into the shared memory message
static void usfstl_shared_mem_merge_msg(
	const struct usfstl_shared_mem_msg *msg, unsigned int msg_size)
{
	const char *msg_end;
	const struct usfstl_shared_mem_msg_section *section;

	for_each_msg_section(section, msg_end, msg, msg_size)
		usfstl_shared_mem_merge_remote_section(section->name,
						       section->buf,
						       section->size);
	USFSTL_ASSERT_EQ((char *)section, msg_end, "%p");
}

// upon an incoming message, update our notion of the remote participant's
// shared memory
void usfstl_shared_mem_handle_msg(const struct usfstl_shared_mem_msg *msg,
				  unsigned int msg_size, bool do_not_mark_dirty)
{
	struct usfstl_multi_participant *p;
	int i;

	// ignore messages after test completion, as usfstl_alloc memory was freed
	if (!g_usfstl_current_test)
		return;

	// an empty message indicates no change
	if (!msg_size)
		return;

	// the controller keeps the state of each participant
	for_each_participant(p, i) {
		// all waiting participants are now outdated
		if (p->flags & USFSTL_MULTI_PARTICIPANT_WAITING)
			p->flags |= USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED;
	}

	// mark the shared memory message for sending to our parent controller
	if (!do_not_mark_dirty)
		g_usfstl_shared_mem_dirty = true;

	// mark the local view as outdated until we really need to refresh it
	// from the buffer
	g_usfstl_multi_local_participant.flags |=
		USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED;

	usfstl_shared_mem_merge_msg(msg, msg_size);
}

// refresh the local view of the shared memory from the updated remote version
void usfstl_shared_mem_update_local_view(void)
{
	if (g_usfstl_multi_local_participant.flags &
	    USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED) {
		char *msg_end;
		struct usfstl_shared_mem_msg_section *section;
		struct usfstl_shared_mem_section *s;
		int i;

		g_usfstl_multi_local_participant.flags &=
			~USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED;

		for_each_msg_section(section, msg_end, g_usfstl_shared_mem_msg,
				     g_usfstl_shared_mem_msg_size) {
			for_each_shared_mem_section(s, i) {
				if (strncmp(s->name, section->name,
				    sizeof(s->name)) == 0) {
					USFSTL_ASSERT_EQ(SECTION_SIZE(s),
							 section->size, "%u");
					memcpy(s->p_start, section->buf,
					       section->size);
					break;
				}
			}
		}
	}
}

// update the shared memory message according to the local view of the shared
// memory
static bool usfstl_shared_mem_update_msg(void)
{
	bool modified = false;
	struct usfstl_shared_mem_section *s;
	int i;

	for_each_shared_mem_section(s, i)
		modified |= usfstl_shared_mem_merge_local_section(s);
	return modified;
}

// save the local view of the shared memory into the shared memory message
void usfstl_shared_mem_prepare_msg(void)
{
	USFSTL_ASSERT(!(g_usfstl_multi_local_participant.flags &
			USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED));

	// update and check if modified
	if (usfstl_shared_mem_update_msg()) {
		struct usfstl_multi_participant *p;
		int i;

		// the controller keeps the state of each participant
		for_each_participant(p, i)
			p->flags |= USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED;

		g_usfstl_shared_mem_dirty = true;
	}
}
