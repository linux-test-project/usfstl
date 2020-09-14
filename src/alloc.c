/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Note this intentionally doesn't store any pointers inside the allocated
 * blocks, that way, ASAN continues to work for accidental "before pointer"
 * accesses.
 *
 * It's also a very dumb and slow implementation, in particular usfstl_free()
 * and usfstl_realloc() are very expensive.
 */
#include <assert.h>
#include <usfstl/alloc.h>
#include "internal.h"

#define USFSTL_INIT_ALLOCATIONS 1000

static void **g_usfstl_allocations;
static size_t g_n_usfstl_allocations;
static size_t g_usfstl_allocations_next_idx;

static void usfstl_alloc_track(void *ptr)
{
	if (!g_n_usfstl_allocations) {
		g_n_usfstl_allocations = USFSTL_INIT_ALLOCATIONS;
		g_usfstl_allocations = calloc(sizeof(void *),
					      g_n_usfstl_allocations);
		assert(g_usfstl_allocations);
	}

	while (g_usfstl_allocations_next_idx < g_n_usfstl_allocations &&
	       g_usfstl_allocations[g_usfstl_allocations_next_idx])
		g_usfstl_allocations_next_idx++;

	if (g_usfstl_allocations_next_idx == g_n_usfstl_allocations) {
		g_n_usfstl_allocations += USFSTL_INIT_ALLOCATIONS;
		g_usfstl_allocations = realloc(g_usfstl_allocations,
					       sizeof(void *) *
					       g_n_usfstl_allocations);
		assert(g_usfstl_allocations);
		memset(g_usfstl_allocations +
		       g_n_usfstl_allocations -
		       USFSTL_INIT_ALLOCATIONS,
		       0,
		       sizeof(void *) * USFSTL_INIT_ALLOCATIONS);
	}

	g_usfstl_allocations[g_usfstl_allocations_next_idx] = ptr;
}

static void usfstl_alloc_remove(void *ptr)
{
	size_t i;

	if (!ptr)
		return;

	for (i = 0; i < g_n_usfstl_allocations; i++) {
		if (g_usfstl_allocations[i] == ptr) {
			g_usfstl_allocations[i] = NULL;
			/* if freeing at all, optimise for short-lived */
			g_usfstl_allocations_next_idx = i;
			return;
		}
	}
}

void *usfstl_malloc(size_t size)
{
	void *ret = malloc(size);

	if (ret)
		usfstl_alloc_track(ret);

	return ret;
}

void *usfstl_calloc(size_t nmemb, size_t size)
{
	size_t len = nmemb * size;
	void *ret = usfstl_malloc(len);

	if (ret)
		memset(ret, 0, len);

	return ret;
}

void *usfstl_realloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);

	if (ret == ptr || !ret)
		return ret;

	usfstl_alloc_remove(ptr);
	usfstl_alloc_track(ret);

	return ret;
}

char *usfstl_strdup(const char *s)
{
	size_t len = strlen(s);
	char *ret = usfstl_malloc(len + 1);

	if (ret) {
		memcpy(ret, s, len);
		ret[len] = 0;
	}

	return ret;
}

char *usfstl_strndup(const char *s, size_t n)
{
	size_t len = strnlen(s, n);
	char *ret = usfstl_malloc(len + 1);

	if (ret) {
		memcpy(ret, s, len);
		ret[len] = 0;
	}

	return ret;
}

void usfstl_free(void *ptr)
{
	free(ptr);
	usfstl_alloc_remove(ptr);
}

void usfstl_free_all(void)
{
	size_t i;

	if (!g_usfstl_allocations)
		return;

	for (i = 0; i < g_n_usfstl_allocations; i++)
		free(g_usfstl_allocations[i]);

	free(g_usfstl_allocations);
	g_usfstl_allocations = NULL;
	g_n_usfstl_allocations = 0;
	g_usfstl_allocations_next_idx = 0;
}
