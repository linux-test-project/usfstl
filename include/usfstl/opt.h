/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * USFSTL test command line options
 */
#ifndef _USFSTL_OPT_H_
#define _USFSTL_OPT_H_
#include <stdbool.h>
#include <stdint.h>

struct usfstl_opt {
	const char *desc;
	const char *long_name;
	const char *argname;
	char short_name;
	bool *flag;
	bool (*handler)(struct usfstl_opt *opt, const char *arg);
	void *data;
};

bool usfstl_opt_parse_int(struct usfstl_opt *opt, const char *arg);
bool usfstl_opt_parse_u64(struct usfstl_opt *opt, const char *arg);
bool usfstl_opt_parse_float(struct usfstl_opt *opt, const char *arg);
bool usfstl_opt_parse_str(struct usfstl_opt *opt, const char *arg);

#define __USFSTL_OPT(l, ...)					\
	static const struct usfstl_opt _usfstl_opt_##l = {	\
		__VA_ARGS__					\
	};							\
	static const struct usfstl_opt * const _usfstl_optp##l	\
	__attribute__((used, section("usfstl_opt")))		\
	= &_usfstl_opt_##l
#define _USFSTL_OPT(l, ...) __USFSTL_OPT(l, __VA_ARGS__)

/*
 * USFSTL_OPT - declare an argument to the binary
 */
#define USFSTL_OPT(_long, _short, _arg, _handler, _data, _desc)	\
	_USFSTL_OPT(__LINE__,					\
		.desc = _desc,					\
		.long_name = _long,				\
		.argname = _arg,				\
		.short_name = _short,				\
		.handler = _handler,				\
		.data = _data,					\
	)

/*
 * USFSTL_OPT_INT - declare an integer argument to the binary
 */
#define USFSTL_OPT_INT(_long, _short, _arg, _val, _desc)	\
	USFSTL_BUILD_BUG_ON(					\
		!__builtin_types_compatible_p(int,		\
					      typeof(_val)) &&	\
		!__builtin_types_compatible_p(unsigned int,		\
					      typeof(_val)));	\
	_USFSTL_OPT(__LINE__,					\
		.desc = _desc,					\
		.long_name = _long,				\
		.short_name = _short,				\
		.argname = _arg,				\
		.handler = usfstl_opt_parse_int,		\
		.data = &_val,					\
	)

/*
 * USFSTL_OPT_U64 - declare an integer argument to the binary
 */
#define USFSTL_OPT_U64(_long, _short, _arg, _val, _desc)	\
	USFSTL_BUILD_BUG_ON(					\
		!__builtin_types_compatible_p(uint64_t,		\
					      typeof(_val)));	\
	_USFSTL_OPT(__LINE__,					\
		.desc = _desc,					\
		.long_name = _long,				\
		.short_name = _short,				\
		.argname = _arg,				\
		.handler = usfstl_opt_parse_u64,		\
		.data = &_val,					\
	)

/*
 * USFSTL_OPT_FLOAT - declare a float argument to the binary
 */
#define USFSTL_OPT_FLOAT(_long, _short, _arg, _val, _desc)	\
	USFSTL_BUILD_BUG_ON(					\
		!__builtin_types_compatible_p(float,		\
					      typeof(_val)));	\
	_USFSTL_OPT(__LINE__,					\
		.desc = _desc,					\
		.long_name = _long,				\
		.short_name = _short,				\
		.argname = _arg,				\
		.handler = usfstl_opt_parse_float,		\
		.data = &_val,					\
	)

/*
 * USFSTL_OPT_FLAG - declare a flag argument to the binary
 */
#define USFSTL_OPT_FLAG(_long, _short, _val, _desc)		\
	USFSTL_BUILD_BUG_ON(					\
		!__builtin_types_compatible_p(bool,		\
					      typeof(_val)));	\
	_USFSTL_OPT(__LINE__,					\
		.desc = _desc,					\
		.long_name = _long,				\
		.short_name = _short,				\
		.flag = &_val,					\
	)

/*
 * USFSTL_OPT_STR - declare a string argument to the binary
 */
#define USFSTL_OPT_STR(_long, _short, _arg, _val, _desc)	\
	USFSTL_BUILD_BUG_ON(					\
		!__builtin_types_compatible_p(const char *,	\
					      typeof(_val)) &&	\
		!__builtin_types_compatible_p(char *,		\
					      typeof(_val)));	\
	_USFSTL_OPT(__LINE__,					\
		.desc = _desc,					\
		.long_name = _long,				\
		.short_name = _short,				\
		.argname = _arg,				\
		.handler = usfstl_opt_parse_str,		\
		.data = &_val,					\
	)

/* must call this if not using the main usfstl code */
int usfstl_parse_options(int argc, char **argv);

#endif // _USFSTL_OPT_H_
