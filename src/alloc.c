/*
 * Copyright (C) 2020, 2022 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Note this intentionally doesn't store any pointers inside the allocated
 * blocks, that way, ASAN continues to work for accidental "before pointer"
 * accesses.
 */
#include <usfstl/alloc.h>
#include <usfstl/uthash.h>
#include "internal.h"

struct alloc_elem {
	void *key;
	UT_hash_handle hh; /* makes this structure hashable */
};

static struct alloc_elem *alloc_elems_head = NULL;

static void usfstl_alloc_track(void *ptr)
{
	struct alloc_elem *elem = NULL;

	elem = (void *)calloc(1, sizeof(*elem));
	USFSTL_ASSERT(elem != NULL,
		      "Failed to allocate memory to alloc structure");
	elem->key = ptr;
	HASH_ADD_PTR(alloc_elems_head, key, elem);
}

static void usfstl_alloc_remove(void *ptr)
{
	struct alloc_elem *elem;

	if (!ptr)
		return;

	HASH_FIND_PTR(alloc_elems_head, &ptr, elem);
	USFSTL_ASSERT(elem && elem->key == ptr,
		      "didn't find the pointer to remove (freeing a non-usfstl-alloc'ed pointer)");
	HASH_DELETE(hh, alloc_elems_head, elem);
	free(elem);
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
	usfstl_alloc_remove(ptr);

	ptr = realloc(ptr, size);

	if (ptr)
		usfstl_alloc_track(ptr);

	return ptr;
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
	usfstl_alloc_remove(ptr);
	free(ptr);
}

void usfstl_free_all(void)
{
	struct alloc_elem *elem, *tmp;

	HASH_ITER(hh, alloc_elems_head, elem, tmp) {
		HASH_DEL(alloc_elems_head, elem);
		free(elem->key);
		free(elem);
	}
	HASH_CLEAR(hh, alloc_elems_head);
}
