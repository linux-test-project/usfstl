/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <usfstl/test.h>
#include "internal.h"

#ifdef USFSTL_USE_FUZZING
static char *g_usfstl_fuzz_repro;
USFSTL_OPT_STR("fuzz-repro", 0, "filename", g_usfstl_fuzz_repro,
	       "data file for reproducing a fuzzer problem");

#if USFSTL_USE_FUZZING == 1
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
#elif USFSTL_USE_FUZZING == 4
#define __AFL_INIT() do { } while (0)
#endif /* USFSTL_USE_FUZZING == 1 */
#endif

#if USFSTL_USE_FUZZING == 3
const unsigned char *USFSTL_NORESTORE_VAR(g_usfstl_fuzz_data);
size_t USFSTL_NORESTORE_VAR(g_usfstl_fuzz_datasz);
#endif

void usfstl_fuzz(const unsigned char **data, size_t *len)
{
#ifdef USFSTL_USE_FUZZING
	size_t bufsz = 1024;
#if USFSTL_USE_FUZZING == 1 || USFSTL_USE_FUZZING == 2 || USFSTL_USE_FUZZING == 4
	size_t bytes, offs = 0;
#endif
	int fd = 0; /* stdin */
	unsigned char *buf = malloc(bufsz);

	assert(buf);

	if (g_usfstl_fuzz_repro) {
		fd = open(g_usfstl_fuzz_repro, O_RDONLY);
		assert(fd >= 0);
#if USFSTL_USE_FUZZING == 4
	} else {
		USFSTL_ASSERT(0, "only built for reproducers, use --fuzz-repro");
#endif // USFSTL_USE_FUZZING == 4
	}

#if USFSTL_USE_FUZZING == 3
	*data = g_usfstl_fuzz_data;
	*len = g_usfstl_fuzz_datasz;
#elif USFSTL_USE_FUZZING == 1 || USFSTL_USE_FUZZING == 2 || USFSTL_USE_FUZZING == 4
	__AFL_INIT();

	while ((bytes = read(fd, buf + offs, bufsz - offs)) > 0) {
		offs += bytes;

		if (bufsz - offs == 0) {
			bufsz *= 2;
			buf = realloc(buf, bufsz);
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
#if USFSTL_USE_FUZZING == 1 || USFSTL_USE_FUZZING == 2
	if (!g_usfstl_fuzz_repro)
		exit(0);
#endif
#endif // USFSTL_USE_FUZZING
}
