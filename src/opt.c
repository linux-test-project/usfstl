/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <stdio.h>
#include <usfstl/opt.h>
#include <usfstl/assert.h>
#include <getopt.h>

extern struct usfstl_opt *__start_usfstl_opt[];
extern struct usfstl_opt *__stop_usfstl_opt;

bool usfstl_opt_parse_int(struct usfstl_opt *opt, const char *arg)
{
	int *ptr = opt->data;
	unsigned long val;
	char *end = NULL;

	if (!arg)
		return false;

	val = strtoul(arg, &end, 0);
	if (*end)
		return false;

	*ptr = val;
	return true;
}

bool usfstl_opt_parse_u64(struct usfstl_opt *opt, const char *arg)
{
	uint64_t *ptr = opt->data;
	unsigned long long val;
	char *end = NULL;

	if (!arg)
		return false;

	val = strtoull(arg, &end, 0);
	if (*end)
		return false;

	*ptr = val;
	return true;
}

bool usfstl_opt_parse_float(struct usfstl_opt *opt, const char *arg)
{
	float *ptr = opt->data, val;
	char *end = NULL;

	if (!arg)
		return false;

	val = strtof(arg, &end);
	if (*end)
		return false;

	*ptr = val;
	return true;
}

bool usfstl_opt_parse_str(struct usfstl_opt *opt, const char *arg)
{
	const char **ptr = opt->data;

	*ptr = arg;

	return true;
}

static void print_options(void)
{
	unsigned int n = &__stop_usfstl_opt - __start_usfstl_opt;
	unsigned int i;

	printf("Command line parameters:\n");
	for (i = 0; i < n; i++) {
		if (!__start_usfstl_opt[i])
			continue;
		printf("  ");
		printf("--%s%s%s%s",
		       __start_usfstl_opt[i]->long_name,
		       __start_usfstl_opt[i]->argname ? "=<" : "",
		       __start_usfstl_opt[i]->argname ?: "",
		       __start_usfstl_opt[i]->argname ? ">" : "");
		if (__start_usfstl_opt[i]->short_name)
			printf(", -%c%s%s%s", __start_usfstl_opt[i]->short_name,
		       __start_usfstl_opt[i]->argname ? "<" : "",
		       __start_usfstl_opt[i]->argname ?: "",
		       __start_usfstl_opt[i]->argname ? ">" : "");
		printf("\n                 %s\n", __start_usfstl_opt[i]->desc);
	}
}

static bool opt_help(struct usfstl_opt *opt, const char *arg)
{
	/* this causes a printout of the options */
	return false;
}

int usfstl_parse_options(int argc, char **argv)
{
	unsigned int n = &__stop_usfstl_opt - __start_usfstl_opt;
	unsigned int i, j;
	struct option *opts = calloc(n + 1, sizeof(struct option));
	char *short_opts = calloc(n + 1, 2), *s_iter = short_opts;
	int ret = 0, idx, val = -1, c;

	USFSTL_ASSERT(opts);
	USFSTL_ASSERT(s_iter);

	for (i = 0, j = 0; i < n; i++) {
		if (!__start_usfstl_opt[i])
			continue;
		opts[j].name = __start_usfstl_opt[i]->long_name;
		opts[j].val = i;
		opts[j].flag = &val;

		if (__start_usfstl_opt[i]->argname)
			opts[j].has_arg = required_argument;
		else
			opts[j].has_arg = no_argument;

		if (__start_usfstl_opt[i]->short_name) {
			*s_iter++ = __start_usfstl_opt[i]->short_name;
			if (opts[j].has_arg == required_argument)
				*s_iter++ = ':';
		}

		j++;
	}

	while (1) {
		idx = -1;
		c = getopt_long(argc, argv, short_opts, opts, &idx);
		if (c == -1)
			break;

		if (idx == -1) {
			for (i = 0; i < n; i++) {
				if (!__start_usfstl_opt[i])
					continue;
				if (c == __start_usfstl_opt[i]->short_name) {
					idx = i;
					break;
				}
			}
		} else {
			// translate opts[] index to __start_usfstl_opt[] index
			idx = opts[idx].val;
		}
		USFSTL_ASSERT(idx != -1);

		if (__start_usfstl_opt[idx]->flag) {
			*__start_usfstl_opt[idx]->flag = true;
			continue;
		}
		if (__start_usfstl_opt[idx]->handler &&
		    !__start_usfstl_opt[idx]->handler(__start_usfstl_opt[idx], optarg)) {
			print_options();
			ret = 2;
			break;
		}
	}

	/* we have no positional arguments */
	if (optind < argc) {
		print_options();
		ret = 2;
	}

	free(short_opts);
	free(opts);

	return ret;
}

USFSTL_OPT("help", '?', false, opt_help, NULL,
	   "print help menu");
