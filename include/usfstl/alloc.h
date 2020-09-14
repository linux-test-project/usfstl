/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _USFSTL_ALLOC_H
#define _USFSTL_ALLOC_H
#include <stdlib.h>
#include <string.h>

void *usfstl_malloc(size_t size);
void *usfstl_calloc(size_t nmemb, size_t size);
void *usfstl_realloc(void *ptr, size_t size);

char *usfstl_strdup(const char *s);
char *usfstl_strndup(const char *s, size_t n);

void usfstl_free(void *ptr);

#endif // _USFSTL_ALLOC_H
