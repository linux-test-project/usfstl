/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "internal.h"

#ifdef USFSTL_WANT_NO_ASAN_STRING

/*
 * strcmp() is sort of expensive if we're linked with ASAN
 * because it ensures the string is terminated by calling
 * strlen() on it first. Open-coding it really gets us the
 * short-circuiting behaviour it should have.
 */
int _usfstl_no_asan_strcmp(const char *_s1, const char *_s2)
{
	const unsigned char *s1 = (void *)_s1;
	const unsigned char *s2 = (void *)_s2;

	for (; *s1 && *s1 == *s2; s1++, s2++) {}

	return (*s1 == *s2) ? 0 : (*s1 < *s2) ? -1 : 1;
}

int (*usfstl_no_asan_strcmp)(const char *s1, const char *s2) =
	_usfstl_no_asan_strcmp;

void *_usfstl_no_asan_memcpy(void *dest, const void *src, size_t n)
{
	size_t i;
	uint8_t *dc = dest;
	uint64_t *d = dest;
	const uint8_t *sc = src;
	const uint64_t *s = src;
	size_t qword_sz = n / 8;

	for (i = 0; i < qword_sz; i++)
		d[i] = s[i];

	// copy the remainder
	if (n & 4)
		*(uint32_t *)&dc[n & ~0x7] = *(const uint32_t *)&sc[n & ~0x7];
	if (n & 2)
		*(uint16_t *)&dc[n & ~0x3] = *(const uint16_t *)&sc[n & ~0x3];
	if (n & 1)
		*(uint8_t *)&dc[n & ~0x1] = *(const uint8_t *)&sc[n & ~0x1];

	return dest;
}

void *(*usfstl_no_asan_memcpy)(void *dest, const void *src, size_t n) =
	_usfstl_no_asan_memcpy;

#ifdef __linux__
#include <assert.h>
#include <dlfcn.h>

static const char *libs[] = {
	"libc.so.6",
};

void usfstl_no_asan_string_init(void)
{
	unsigned int i;

	for (i = 0; i < sizeof(libs) / sizeof(libs[0]); i++) {
		void *lib;
		void *sym;

		lib = dlopen(libs[i], RTLD_LAZY);
		if (!lib)
			continue;

		sym = dlsym(lib, "strcmp");
		if (sym)
			usfstl_no_asan_strcmp = sym;

		sym = dlsym(lib, "memcpy");
		if (sym)
			usfstl_no_asan_memcpy = sym;
	}
}
#else
void usfstl_no_asan_string_init(void)
{
}
#endif // __linux__

#endif // _USFSTL_HAVE_ASAN_STRING
