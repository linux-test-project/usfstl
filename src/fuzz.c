/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <usfstl/test.h>
#include <usfstl/alloc.h>
#include "internal.h"

#ifdef USFSTL_USE_FUZZING
static char *g_usfstl_fuzz_repro;
USFSTL_OPT_STR("fuzz-repro", 0, "filename", g_usfstl_fuzz_repro,
	       "data file for reproducing a fuzzer problem");
#ifndef _WIN32
/* list only helps if we have fork() */
#include <sys/wait.h>
static char *g_usfstl_fuzz_repro_list;
USFSTL_OPT_STR("fuzz-repro-list", 0, "filename", g_usfstl_fuzz_repro_list,
	       "file containing filenames to run for fuzz reproduction, one per line");
static int g_usfstl_fuzz_repro_parallel, g_usfstl_fuzz_repro_running;
USFSTL_OPT_INT("fuzz-repro-parallel", 0, "number-of-jobs",
	       g_usfstl_fuzz_repro_parallel,
	       "how many parallel fork()s to run for reproduction");
#define FUZZ_OPEN_FLAGS O_RDONLY
#else /* _WIN32 */
#define FUZZ_OPEN_FLAGS (O_RDONLY | O_BINARY)
#endif /* _WIN32 */

#if defined(USFSTL_FUZZER_AFL_GCC)
extern int __afl_setup_failure;

static void __attribute__((noinline)) force_afl_start(void)
{
	/* nothing - just have a function that forces AFL annotation */
}

static void __attribute__((noinline)) __afl_init(bool start)
{
	char shmid[20] = "__AFL_SHM_IX";
	char *val;

	/*
	 * This is a hack so the compiler won't optimise it,
	 * because we manipulate the binary ...
	 */
	if (start)
		shmid[11] = 'D';

	val = getenv(shmid);
	if (val) {
		setenv("__AFL_SHM_IX", val, 1);
		__afl_setup_failure = 0;
	}

	force_afl_start();
}

#define __AFL_INIT() __afl_init(true)
#elif defined(USFSTL_FUZZER_REPRO)
#define __AFL_INIT() do { } while (0)
#endif /* defined(USFSTL_FUZZER_AFL_GCC) */
#endif

#if defined(USFSTL_FUZZER_LIB_FUZZER)
const unsigned char *USFSTL_NORESTORE_VAR(g_usfstl_fuzz_data);
size_t USFSTL_NORESTORE_VAR(g_usfstl_fuzz_datasz);
#endif

void usfstl_fuzz(const unsigned char **data, size_t *len)
{
#ifdef USFSTL_USE_FUZZING
	size_t bufsz = 1024;
#if defined(USFSTL_FUZZER_AFL_GCC) || defined(USFSTL_FUZZER_AFL_CLANG_FAST) || defined(USFSTL_FUZZER_REPRO) || defined(USFSTL_FUZZER_AFL_GCC_FAST)
	size_t bytes, offs = 0;
#endif
	int fd = 0; /* stdin */
	unsigned char *buf = usfstl_malloc(bufsz);

	assert(buf);

#ifndef _WIN32
	if (g_usfstl_fuzz_repro_list) {
		FILE *names;
		char file[4096];
		pid_t p;

		USFSTL_ASSERT(!g_usfstl_fuzz_repro,
			      "cannot use both --fuzz-repro/--fuzz-repro-list");

		if (strcmp(g_usfstl_fuzz_repro_list, "-") == 0)
			names = stdin;
		else
			names = fopen(g_usfstl_fuzz_repro_list, "r");
		USFSTL_ASSERT(names, "failed to open %s",
			      g_usfstl_fuzz_repro_list);

		while (fgets(file, sizeof(file), names)) {
			size_t len = strlen(file);

			if (file[len - 1] == '\n')
				file[len - 1] = 0;

			printf("fuzz-repro: starting %s\n", file);

			fd = open(file, O_RDONLY);
			USFSTL_ASSERT(fd >= 0);
			p = fork();
			USFSTL_ASSERT(p >= 0);
			if (p == 0)
				goto fuzz;

			close(fd);

			g_usfstl_fuzz_repro_running++;

			// if needed, wait for one child
			if (g_usfstl_fuzz_repro_running >= g_usfstl_fuzz_repro_parallel) {
				wait(NULL);
				g_usfstl_fuzz_repro_running--;
			}
		}

		while (g_usfstl_fuzz_repro_running--)
			wait(NULL);

		fclose(names);

		// abort the parent test run - easier than detecting last file
		exit(0);
fuzz:;
	} else
#endif // _WIN32
	if (g_usfstl_fuzz_repro) {
		fd = open(g_usfstl_fuzz_repro, FUZZ_OPEN_FLAGS);
		assert(fd >= 0);
#if defined(USFSTL_FUZZER_REPRO)
	} else {
		USFSTL_ASSERT(0, "only built for reproducers, use --fuzz-repro");
#endif // defined(USFSTL_FUZZER_REPRO)
	}

#if defined(USFSTL_FUZZER_LIB_FUZZER)
	*data = g_usfstl_fuzz_data;
	*len = g_usfstl_fuzz_datasz;
#elif defined(USFSTL_FUZZER_AFL_GCC) || defined(USFSTL_FUZZER_AFL_CLANG_FAST) || defined(USFSTL_FUZZER_REPRO) || defined(USFSTL_FUZZER_AFL_GCC_FAST)
	__AFL_INIT();

	while ((bytes = read(fd, buf + offs, bufsz - offs)) > 0) {
		offs += bytes;

		if (bufsz - offs == 0) {
			bufsz *= 2;
			buf = usfstl_realloc(buf, bufsz);
			assert(buf);
		}
	}

	*data = buf;
	*len = offs;

	if (g_usfstl_fuzz_repro)
		close(fd);
#else
#error "don't know how to get data from this fuzzer"
#endif
#else
	USFSTL_ASSERT(0, "Need to compile with fuzzing support");
#endif
}

void usfstl_fuzz_test_ok(void)
{
#ifdef USFSTL_USE_FUZZING
#if defined(USFSTL_FUZZER_AFL_GCC) || defined(USFSTL_FUZZER_AFL_CLANG_FAST)
	if (!g_usfstl_fuzz_repro)
		exit(0);
#endif
#endif // USFSTL_USE_FUZZING
}
