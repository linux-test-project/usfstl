/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _USFSTL_ASSERT_H_
#define _USFSTL_ASSERT_H_
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include "macros.h"
#include "uthash.h"

#define USFSTL_ASSERT_MAX_KEY_LEN	500

struct usfstl_assert_profiling_info {
	const char *condition;
	const char *reqfmt;
	const char *file;
	char key[USFSTL_ASSERT_MAX_KEY_LEN];
	uint32_t line;
	uint32_t count;
	UT_hash_handle hh;
};

#ifdef USFSTL_USE_ASSERT_PROFILING
#define USFSTL_PROFILE_ASSERT(__condition, __reqfmt)				\
	do {									\
		static struct usfstl_assert_profiling_info			\
		__usfstl_assert_info = {					\
			.condition = #__condition,				\
			.reqfmt = __reqfmt,					\
			.file = __FILE__,					\
			.line = __LINE__,					\
			.count = 0						\
		};								\
		static struct usfstl_assert_profiling_info			\
		*__usfstl_assert_info_ptr					\
		__attribute__((used,						\
			       section("usfstl_assert_profiling_info"))) =	\
			&__usfstl_assert_info;					\
		__usfstl_assert_info.count++;					\
	} while (0)
#else
#define USFSTL_PROFILE_ASSERT(__condition, __reqfmt)  do {} while (0)
#endif /* USFSTL_USE_ASSERT_PROFILING */

/*
 * usfstl_abort - abort a test and print the given message(s)
 */
void __attribute__((noreturn, format(printf, 4, 5)))
usfstl_abort(const char *fn, unsigned int line, const char *cond, const char *msg, ...);

#define _USFSTL_ASSERT_0(cstr, cond) do {				\
	USFSTL_PROFILE_ASSERT(cond, "");				\
	if (!(cond))							\
		usfstl_abort(__FILE__, __LINE__, cstr, "");		\
} while (0)

#define _USFSTL_ASSERT_1(cstr, cond, msg, ...) do {			\
	USFSTL_PROFILE_ASSERT(cond, msg);				\
	if (!(cond))							\
		usfstl_abort(__FILE__, __LINE__, cstr,			\
			     msg, ##__VA_ARGS__);			\
} while (0)

/*
 * assert, with or without message
 *
 * USFSTL_ASSERT(cond)
 * USFSTL_ASSERT(cond, msg, format)
 */
#define USFSTL_ASSERT(cond, ...)	\
	usfstl_dispatch_has(_USFSTL_ASSERT_, __VA_ARGS__)(#cond, cond, ##__VA_ARGS__)

#define _USFSTL_ASSERT_CMP_0(as, a, op, bs, b, fmt) do {		\
	typeof(a) _a = a;						\
	typeof(b) _b = b;						\
	if (!((_a) op (_b)))						\
		usfstl_abort(__FILE__, __LINE__, as " " #op " " bs,	\
			     "  " as " = " fmt "\n  " bs " = " fmt "\n",\
			     _a, _b);					\
} while (0)

#define _USFSTL_ASSERT_CMP_1(as, a, op, bs, b, fmt, prfn) do {		\
	typeof(a) _a = a;						\
	typeof(b) _b = b;						\
	if (!((_a) op (_b)))						\
		usfstl_abort(__FILE__, __LINE__, as " " #op " " bs,	\
			     "  " as " = " fmt "\n  " bs " = " fmt "\n",\
			     prfn(_a), prfn(_b));			\
} while (0)

/*
 * Assert that two values are equal.
 *
 * Note that this is a special case of USFSTL_ASSERT_CMP() below, so the
 * documentation for that applies.
 */
#define USFSTL_ASSERT_EQ(a, b, fmt, ...)				\
	usfstl_dispatch_has(_USFSTL_ASSERT_CMP_, __VA_ARGS__)(#a, a, ==,\
							      #b, b, fmt,\
							      ##__VA_ARGS__)

/*
 * Assert a comparison is true.
 *
 * Given a value, comparison operator and another value it checks that
 * the comparison is true, and aborts the test (or program, if used
 * outside a test) otherwise.
 *
 * You must pass a format string suitable for printing the values.
 *
 * You may additionally pass a formatting macro that evaluates the
 * data for the format string, e.g.
 *
 * #define val_and_addr(x) (x), &(x)
 * ...
 * int x = 1, y = 2;
 * USFSTL_ASSERT_CMP(x, ==, y, "%d (at %p)", val_and_addr)
 *
 * Will result in the printout
 * [...]
 *   x = 1 (at 0xffff1110)
 *   y = 2 (at 0xffff1114)
 */
#define USFSTL_ASSERT_CMP(a, op, b, fmt, ...)				\
	usfstl_dispatch_has(_USFSTL_ASSERT_CMP_, __VA_ARGS__)(#a, a, op,\
							      #b, b, fmt,\
							      ##__VA_ARGS__)

#endif // _USFSTL_ASSERT_H_
