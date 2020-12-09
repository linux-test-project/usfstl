/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <usfstl/test.h>
#include <usfstl/log.h>
#include "internal.h"

// keep this a power of two - optimise the "hash function"
#define MAX_OVERRIDES 1024

#define HASH_SIZE (MAX_OVERRIDES*2)
static struct {
	const void *orig, *repl;
} g_usfstl_hash[HASH_SIZE];
/* shift down a bit since functions are typically aligned */
#define HASH_PTR(orig) ((((uintptr_t)orig) >> 4) % HASH_SIZE)

static struct {
	const char *name;
	const void *repl;
} g_usfstl_overrides[MAX_OVERRIDES] = {};
static unsigned int g_usfstl_num_overrides;

void find_info(const char *filename, const char *funcname,
	       const char **rettype, const char **args)
{
	if (usfstl_get_func_info(filename, funcname, rettype, args) == 0)
		return;

	if (usfstl_get_func_info(NULL, funcname, rettype, args) == 0)
		return;

	usfstl_printf("ERROR: don't know ret/arg types for function %s (from %s)\n", funcname, filename ?: "<unknown>");
	fflush(stdout);
	abort();
}

void usfstl_install_stub(const char *fname, const void *repl, const char *__replname,
			 const char *file, unsigned int line, bool check_args)
{
	const char *orig_ret, *orig_args;
	const char *repl_ret, *repl_args;
	char replname[1000], replfile[1000];
	unsigned int i, replline;

	/* reset the hash if we change the stubs */
	memset(&g_usfstl_hash, 0, sizeof(g_usfstl_hash));

	if (repl) {
		usfstl_get_function_info(repl, replname, replfile, &replline);

		if (repl == &usfstl_void_stub) {
			repl_ret = "void";
			repl_args = NULL;
			assert(!check_args);
		} else {
			find_info(replfile, replname, &repl_ret, &repl_args);
		}

		find_info(NULL, fname, &orig_ret, &orig_args);

		if (strcmp(repl_ret, orig_ret)) {
			fprintf(stderr, "incompatible return types at %s:%d:\n", file, line);
			fprintf(stderr, "  %s %s(%s)\n", orig_ret, fname, orig_args ?: "void");
			fprintf(stderr, "  %s %s(%s)\n", repl_ret, replname, repl_args ?: "void");
			fprintf(stderr, "  (declared at %s:%d)\n", replfile, replline);
			assert(0);
		}

		if (check_args && strcmp(repl_args, orig_args)) {
			fprintf(stderr, "incompatible argument types at %s:%d:\n", file, line);
			fprintf(stderr, "  %s %s(%s)\n", orig_ret, fname, orig_args);
			fprintf(stderr, "  %s %s(%s)\n", repl_ret, replname, repl_args);
			fprintf(stderr, "  (declared at %s:%d)\n", replfile, replline);
			assert(0);
		}
	}

	for (i = 0; i < g_usfstl_num_overrides; i++) {
		if (!strcmp(g_usfstl_overrides[i].name, fname)) {
			/*
			 * allow overriding with the same again, that's
			 * useless but not really a problem.
			 */
			if (g_usfstl_overrides[i].repl == repl) {
				fprintf(stderr,
					"WARNING: %s was replaced again with its existing (%s) function/stub - did you really intend to do this?\n",
					fname,
					repl ? replname : "original");
				return;
			}
			g_usfstl_overrides[i].repl = repl;
			return;
		}
	}

	if (g_usfstl_num_overrides == MAX_OVERRIDES) {
		fprintf(stderr, "!!!!! too many overrides, bump MAX_OVERRIDES (currently %d)\n", MAX_OVERRIDES);
		exit(2);
	}

	g_usfstl_overrides[g_usfstl_num_overrides].repl = repl;
	g_usfstl_overrides[g_usfstl_num_overrides].name = fname;
	g_usfstl_num_overrides++;
}

void usfstl_void_stub(void)
{
}

static bool same_filename(const char *f1, const char *f2)
{
	int i1 = 0, i2 = 0;

	while (f1[i1] && f2[i2]) {
		if (f1[i1] == f2[i2]) {
			i1++;
			i2++;
			continue;
		}

		if ((f1[i1] == '/' || f1[i1] == '\\') &&
		    (f2[i2] == '/' || f2[i2] == '\\')) {
			while (f1[i1] == '/' || f1[i1] == '\\')
				i1++;
			while (f2[i2] == '/' || f2[i2] == '\\')
				i2++;
			continue;
		}

		break;
	}

	return f1[i1] == f2[i2];
}

// compiler/linker generated symbols for the special
// text_test section we put all the test code into
extern unsigned char __start_text_test[];
extern unsigned char __stop_text_test[];

const void *__attribute__((no_instrument_function, noinline))
usfstl_find_repl(const void *_orig)
{
	/* 5 is the size of the generated call instruction (x86) */
	const void *orig = (char *)_orig - 5;
	const void *repl = _orig;
	char fname[1000];
	char filename[1000];
	unsigned int i;

	if (!g_usfstl_current_test)
		return _orig;

	fname[0] = '\0';
	filename[0] = '\0';

	for (i = HASH_PTR(orig); i < HASH_SIZE; i++) {
		if (g_usfstl_hash[i].orig == orig)
			return g_usfstl_hash[i].repl;
	}

	usfstl_get_function_info(orig, fname, filename, NULL);
	assert(*fname);
	assert(*filename);

	for (i = 0; i < g_usfstl_num_overrides; i++) {
		if (!strcmp(g_usfstl_overrides[i].name, fname)) {
			repl = g_usfstl_overrides[i].repl;
			goto hash;
		}
	}

	if (orig >= (void *)__start_text_test && orig < (void *)__stop_text_test)
		goto hash;

	if (g_usfstl_recurse) {
		return repl;
	} else if (g_usfstl_current_test->flow_test) {
		goto hash;
	} else if (!strcmp(filename + strlen(filename) - 2, ".h")) {
		goto hash;
	} else if (g_usfstl_current_test->tested_files) {
		for (i = 0; g_usfstl_current_test->tested_files[i]; i++) {
			const char *okf = g_usfstl_current_test->tested_files[i];

			if ((strlen(okf) <= strlen(filename)) &&
			    (same_filename(filename + strlen(filename) - strlen(okf), okf)))
				goto hash;
		}
	}

	usfstl_abort(__FILE__, __LINE__, "invalid function call",
		   "calling %s from %s!\n", fname, filename);

 hash:
	for (i = HASH_PTR(orig); i < HASH_SIZE; i++) {
		if (!g_usfstl_hash[i].orig) {
			g_usfstl_hash[i].orig = orig;
			g_usfstl_hash[i].repl = repl;
			break;
		}
	}

	return repl;
}

void usfstl_reset_overrides(void)
{
	memset(g_usfstl_overrides, 0, sizeof(g_usfstl_overrides));
	g_usfstl_num_overrides = 0;
	memset(&g_usfstl_hash, 0, sizeof(g_usfstl_hash));
}
