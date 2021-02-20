/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <usfstl/test.h>
#include "internal.h"

// created by the linker
extern char __start_usfstl_norestore[];
extern char __stop_usfstl_norestore[];

struct restore_info {
	uintptr_t ptr, size;
};

// since it's prefixed g_usfstl_, this wouldn't get saved/restored anyhow,
// but we need a single variable to always be in this section, so just
// put it there
static struct restore_info *USFSTL_NORESTORE_VAR(g_usfstl_restore_info);
static void *USFSTL_NORESTORE_VAR(g_usfstl_restore_data);

static inline bool should_restore(uintptr_t _ptr)
{
	char *ptr = (char *)_ptr;

	USFSTL_BUILD_BUG_ON(sizeof(uintptr_t) != sizeof(void *));

	if (ptr >= __start_usfstl_norestore && ptr < __stop_usfstl_norestore)
		return false;

	return true;
}

#ifndef O_BINARY
#define O_BINARY 0
#endif

static struct restore_info *usfstl_read_restore_info(const char *program)
{
	char buf[1000];
	int fd;
	off_t len;
	struct restore_info *info, *iter, *out = NULL;
	uintptr_t base = usfstl_dwarf_get_base_address();
	ssize_t r;
	uintptr_t prev = 0;

	assert(snprintf(buf, sizeof(buf), "%s.globals", program) < (int)sizeof(buf));

	fd = open(buf, O_RDONLY | O_BINARY);
	assert(fd >= 0);

	len = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	assert((len % sizeof(struct restore_info)) == 0);

	/* this allocates as much as we might need in the worst case */
	info = calloc(len / sizeof(struct restore_info) + 1,
		      sizeof(struct restore_info));
	assert(info);

	r = read(fd, info, len);
	assert(r == len);

	for (iter = info; iter->ptr != 0; iter++) {
		unsigned long ptr = iter->ptr + base;

		/* ensure that the list is sorted */
		USFSTL_ASSERT((unsigned long)ptr > prev);
		prev = ptr;

		if (!should_restore(ptr))
			continue;

		if (out && out->ptr + out->size == ptr) {
			out->size += iter->size;
			continue;
		} else if (!out) {
			out = info;
		} else {
			out++;
		}

		out->ptr = ptr;
		out->size = iter->size;
	}

	/* terminate */
	if (!out)
		out = info;
	else
		out++;
	out->ptr = 0;
	out->size = 0;

	close(fd);

	return info;
}

static void *usfstl_save_restore_data(struct restore_info *info)
{
#if USFSTL_USE_FUZZING == 1 || USFSTL_USE_FUZZING == 2
	/* not needed for AFL fork-based fuzzing */
	return NULL;
#else
	struct restore_info *iter = info;
	unsigned long long total = 0;
	unsigned char *data, *ret;

	while (iter->ptr || iter->size) {
		total += iter->size;
		iter++;
	}

	data = malloc(total);
	ret = data;
	assert(data);

	iter = info;
	while (iter->ptr || iter->size) {
		memcpy(data, (void *)(uintptr_t)iter->ptr, iter->size);
		data += iter->size;
		iter++;
	}

	return ret;
#endif
}

static void usfstl_restore_data(struct restore_info *info, const void *_data)
{
#if USFSTL_USE_FUZZING == 1 || USFSTL_USE_FUZZING == 2
	/* not needed for AFL fork-based fuzzing */
#else
	struct restore_info *iter = info;
	const unsigned char *data = _data;

	while (iter->ptr || iter->size) {
		memcpy((void *)(uintptr_t)iter->ptr, data, iter->size);
		data += iter->size;
		iter++;
	}
#endif
}

void usfstl_save_globals(const char *program)
{
	g_usfstl_restore_info = usfstl_read_restore_info(program);
	g_usfstl_restore_data = usfstl_save_restore_data(g_usfstl_restore_info);
}

void usfstl_restore_globals(void)
{
	usfstl_restore_data(g_usfstl_restore_info, g_usfstl_restore_data);
}
