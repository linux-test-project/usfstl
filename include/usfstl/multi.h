/*
 * Copyright (C) 2019 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * usfstl multi-process simulation support
 *
 * You can simulate multiple components with combined schedulers etc. using the
 * support code in this module. The controller process contains the test cases
 * and handles orchestration, while the remaining simulation participants will
 * not have any test cases but will be invoked for the test cases using RPC.
 *
 * In a controlled process, the test cases will be invoked with RPC, and will
 * simply handle RPC messages in a loop; however, the pre/post functions can
 * still be used by assigning the global variables declared below.
 */
#ifndef _USFSTL_MULTI_H
#define _USFSTL_MULTI_H
#include <stdint.h>
#include "rpc.h"
#include "sched.h"
#include "test.h"

enum usfstl_multi_participant_flags {
	USFSTL_MULTI_PARTICIPANT_WAITING		= 1 << 0,
	/* indicates that the (local/remote) participant's view of the shared mem is outdated */
	USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED	= 1 << 1,
};

/**
 * struct usfstl_multi_participant - RPC test participant
 * @name: name of the participant, set automatically if you use the
 *	USFSTL_MULTI_PARTICIPANT() macro below
 * @conn: connection to the participant
 * @binary: binary to run (if @pre_connected isn't set)
 * @args: NULL-terminated array of extra arguments to pass to the
 *	binary to run (if any)
 * @job: multi-scheduler job for this participant, you can set the
 *	@job.priority if you like
 * @pre_connected: if @conn is already valid, set this; otherwise the
 *	@binary will be invoked to run the participant
 * @flags: see &enum usfstl_multi_participant_flags
 * @sync_set: indicates @sync was set (and participant notified)
 * @sync: next sync time for this participant
 * @data: arbitrary data pointer for use by the application
 */
struct usfstl_multi_participant {
	const char *name;
	struct usfstl_rpc_connection *conn;
	const char *binary;
	const char * const *args;
	struct usfstl_job job;
	uint32_t pre_connected:1, sync_set:1;
	uint32_t flags;
	uint64_t sync;
	unsigned int pid;
	void *data;
};

#define USFSTL_MULTI_PARTICIPANT(_name, ...)				\
static struct usfstl_rpc_connection					\
USFSTL_NORESTORE_VAR(_name ## _conn);					\
static struct usfstl_multi_participant _name = {			\
	.name = #_name,							\
	.conn = &_name ## _conn,					\
	__VA_ARGS__							\
};									\
static const struct usfstl_multi_participant *				\
usfstl_multi_participant_ ## _name					\
	__attribute__((used, section("usfstl_rpcp"))) = &_name

/*
 * Use an USFSTL_INITIALIZER to set the callbacks here for the controlled
 * process test, if needed (you can set pre, post and extra_data).
 */
extern struct usfstl_test g_usfstl_multi_controlled_test;

/*
 * Use an USFSTL_INITIALIZER to set some data here if you like, you can
 * change the .name or .job.priority.
 */
extern struct usfstl_multi_participant g_usfstl_multi_local_participant;

/*
 * connection to the external control simulation,
 * use it when need to send rpc's to it.
 */
extern struct usfstl_rpc_connection *g_usfstl_multi_ctrl_conn;

/**
 * usfstl_multi_add_rpc_connection_sched - add a multi-participant RPC connection
 * @conn: the RPC connection to set up and add
 * @sched: the scheduler to link with
 *
 * This adds a multi-participant simulation RPC connection that can
 * e.g. be used to implement peer-to-peer RPCs between participants
 * or similar. The given scheduler (@sched) is linked to the RPC extra
 * transmit/receive, and will be kept in sync with the remote.
 */
void usfstl_multi_add_rpc_connection_sched(struct usfstl_rpc_connection *conn,
					   struct usfstl_scheduler *sched);

/**
 * usfstl_multi_add_rpc_connection - add a multi-participant RPC connection
 * @conn: the RPC connection to set up and add
 *
 * This adds a multi-participant simulation RPC connection that can
 * e.g. be used to implement peer-to-peer RPCs between participants
 * or similar. Note that the %g_usfstl_top_scheduler is linked, so
 * it might even be used by non-multi parties.
 */
static inline void
usfstl_multi_add_rpc_connection(struct usfstl_rpc_connection *conn)
{
	usfstl_multi_add_rpc_connection_sched(conn, NULL);
}

/**
 * usfstl_multi_get_participant - obtain participant from connection
 * @conn: the RPC connection from which to get the participant
 *
 * Returns: the participant pointer
 *
 * Note: Use this only if you know that the connection is actually
 *	 for a multi-participant simulation participant, there's
 *	 no check on the validity or type.
 */
static inline struct usfstl_multi_participant *
usfstl_multi_get_participant(struct usfstl_rpc_connection *conn)
{
	return conn->data;
}

#endif // _USFSTL_MULTI_H
