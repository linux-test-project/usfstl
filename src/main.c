/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <usfstl/test.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <usfstl/rpc.h>
#include <usfstl/opt.h>
#include "internal.h"
#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <windows.h>
static uint64_t __attribute__((used)) get_monotonic_time_ms(void)
{
	return GetTickCount64();
}
#else
static uint64_t __attribute__((used)) get_monotonic_time_ms(void)
{
	struct timespec t = {0};

	clock_gettime(CLOCK_MONOTONIC, &t);

	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
#endif

// dummy test so you can link a binary with no tests
static const struct usfstl_test * const _dummy_test __attribute__((used, section("usfstl_tests")));
extern struct usfstl_test *__start_usfstl_tests[];
extern struct usfstl_test *__stop_usfstl_tests;

// dummy for always having the section at link time
USFSTL_INITIALIZER(NULL);
extern usfstl_init_fn_t __start_usfstl_initfns[];
extern usfstl_init_fn_t __stop_usfstl_initfns;

const char *USFSTL_NORESTORE_VAR(g_usfstl_program_name);

static int USFSTL_NORESTORE_VAR(g_usfstl_requirements_fd) = -1;
static char *g_usfstl_requirements_file;
bool g_usfstl_skip_known_failing;
bool g_usfstl_flush_each_log;
bool g_usfstl_list_tests;

#ifndef USFSTL_LIBRARY
static bool g_usfstl_list_asserts;
#endif // USFSTL_LIBRARY

#if !defined(USFSTL_LIBRARY) && (!defined(USFSTL_USE_FUZZING) || USFSTL_USE_FUZZING != 3)
static int g_usfstl_test = -1;
static int g_usfstl_testcase = -1;
static int g_usfstl_summary_fd = -1;
static bool USFSTL_NORESTORE_VAR(g_usfstl_count);
static int g_usfstl_testcase_last = -1;
#endif

#if !defined(USFSTL_LIBRARY) && (!defined(USFSTL_USE_FUZZING) || USFSTL_USE_FUZZING != 3)
static bool init_summary(struct usfstl_opt *opt, const char *arg)
{
	int len;

	g_usfstl_summary_fd = open(arg, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (g_usfstl_summary_fd < 0) {
		printf("Couldn't open summary file '%s'\n", arg);
		return false;
	}
	len = strlen(g_usfstl_projectname);
	assert(write(g_usfstl_summary_fd, g_usfstl_projectname, len) == len);
	assert(write(g_usfstl_summary_fd, "\n", 1) == 1);

	return true;
}

static void write_summary(const char *name, unsigned int succ, unsigned int fail)
{
	char buf[1000];
	ssize_t len;

	if (g_usfstl_summary_fd < 0)
		return;

	len = snprintf(buf, sizeof(buf), "%d %d %s\n", succ, fail, name);

	/* we have short strings - they should always succeed */
	assert(write(g_usfstl_summary_fd, buf, len) == len);
}

static void close_summary(void)
{
	if (g_usfstl_summary_fd >= 0)
		close(g_usfstl_summary_fd);
	g_usfstl_summary_fd = -1;
}
#endif

static void usfstl_tested_requirement_count(const char *req,
					    unsigned int pass,
					    unsigned int fail)
{
	char buf[1000];
	ssize_t len;

	if (!g_usfstl_requirements_file)
		return;

	if (g_usfstl_requirements_fd < 0) {
		g_usfstl_requirements_fd = open(g_usfstl_requirements_file,
						O_CREAT | O_WRONLY | O_TRUNC,
						0666);
		assert(g_usfstl_requirements_fd >= 0);
		len = strlen(g_usfstl_projectname);
		assert(write(g_usfstl_requirements_fd, g_usfstl_projectname, len) == len);
		assert(write(g_usfstl_requirements_fd, "\n", 1) == 1);
	}

	len = snprintf(buf, sizeof(buf), "%s\t%s\t%d\t%d\n", g_usfstl_current_test->name, req, pass, fail);
	assert(write(g_usfstl_requirements_fd, buf, len) == len);
}

void usfstl_tested_requirement(const char *req, bool pass)
{
	usfstl_tested_requirement_count(req, pass ? 1 : 0, pass ? 0 : 1);
}

#if !defined(USFSTL_USE_FUZZING) || USFSTL_USE_FUZZING != 3
static void write_requirements(const struct usfstl_test *tc, int tc_succeeded,
			       int tc_failed)
{
	const char * const *req = tc->requirements;

	while (req && *req) {
		usfstl_tested_requirement_count(*req, tc_succeeded,
						tc_failed);
		req++;
	}
}

static void close_requirements(void)
{
	if (g_usfstl_requirements_fd >= 0)
		close(g_usfstl_requirements_fd);
	g_usfstl_requirements_fd = -1;
}
#endif

#if !defined(USFSTL_LIBRARY) && (!defined(USFSTL_USE_FUZZING) || USFSTL_USE_FUZZING != 3)
static bool usfstl_parse_test(struct usfstl_opt *opt, const char *val)
{
	int tcidx, idx = 0;

	if (usfstl_opt_parse_int(opt, val))
		return true;

	for (tcidx = 0; &__start_usfstl_tests[tcidx] < &__stop_usfstl_tests; tcidx++) {
		struct usfstl_test *tc = __start_usfstl_tests[tcidx];

		if (!tc)
			continue;

		if (!strcmp(val, tc->name)) {
			g_usfstl_test = idx;
			return true;
		}

		idx++;
	}

	return false;
}

USFSTL_OPT("test", 't', "test name or number", usfstl_parse_test,
	   &g_usfstl_test, "execute only this test");
USFSTL_OPT_FLAG("count", 0, g_usfstl_count,
		"print total test case count (over all tests)");
USFSTL_OPT_FLAG("list", 'p', g_usfstl_list_tests,
		"print test list with number of cases");
USFSTL_OPT_INT("case", 'c', "case num", g_usfstl_testcase,
	       "execute only this case (should come with -t)");
USFSTL_OPT_INT("last", 'v', "case num", g_usfstl_testcase_last,
	       "execute from the case given with -c to this case (inclusive)");
USFSTL_OPT("summary", 0, "filename", init_summary, NULL,
	   "write summary of tests to this file after running");
USFSTL_OPT_FLAG("skip-known-failing", 0, g_usfstl_skip_known_failing,
		"skip tests/testcases known to fail");
#endif

#ifndef USFSTL_LIBRARY
USFSTL_OPT_FLAG("list-asserts", 0, g_usfstl_list_asserts,
                "list all asserts compiled in the test code");
#endif // USFSTL_LIBRARY

USFSTL_OPT_FLAG("debug", 'd', g_usfstl_abort_on_error,
		"debug mode, abort on any error");
USFSTL_OPT_FLAG("flush", 'f', g_usfstl_flush_each_log,
		"flush every log written (slower runtime but consistent logs on crashes)");
USFSTL_OPT_STR("requirements", 0, "filename", g_usfstl_requirements_file,
	       "write requirement execution tracking to this file");
USFSTL_OPT_FLAG("disable-wdt", 0, g_usfstl_disable_wdt,
		"disable watchdog timer (e.g to enable working with debugger)");

static void usfstl_call_initializers(void)
{
	int i;

	for (i = 0; &__start_usfstl_initfns[i] < &__stop_usfstl_initfns; i++) {
		usfstl_init_fn_t fn = __start_usfstl_initfns[i];

		if (fn)
			fn();
	}
}

#ifndef USFSTL_LIBRARY
static
#endif
int usfstl_init(int argc, char **argv)
{
	int ret;

	g_usfstl_program_name = argv[0];

	ret = usfstl_parse_options(argc, argv);
	if (ret)
		return ret;

	if (g_usfstl_list_tests)
		return 0;

	usfstl_dwarf_init(argv[0]);

	if (g_usfstl_assert_coverage_file)
		usfstl_init_reached_assert_log();

	usfstl_call_initializers();
	usfstl_save_globals(argv[0]);
	usfstl_multi_init();
	usfstl_no_asan_string_init();

	return 0;
}

#if USFSTL_USE_FUZZING == 3
static struct usfstl_test *USFSTL_NORESTORE_VAR(g_usfstl_fuzz_test);

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	int ret = usfstl_init(*argc, *argv);
	int tcidx;

	if (ret)
		return ret;

	for (tcidx = 0; &__start_usfstl_tests[tcidx] < &__stop_usfstl_tests; tcidx++) {
		struct usfstl_test *tc = __start_usfstl_tests[tcidx];

		if (!tc)
			continue;

		g_usfstl_fuzz_test = tc;
		break;
	}

	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	g_usfstl_fuzz_data = data;
	g_usfstl_fuzz_datasz = size;
	usfstl_execute_test(g_usfstl_fuzz_test, 0, 0, true);
	return 0;
}
#elif USFSTL_LIBRARY
// can be overridden by the user if they need to
const char * const g_usfstl_projectname = "libtest";

enum usfstl_testcase_status usfstl_run_test(const struct usfstl_test *tc)
{
	enum usfstl_testcase_status status;

	status = usfstl_execute_test(tc, 0, 0, true);

	USFSTL_ASSERT_CMP((int)status, !=, (int)USFSTL_STATUS_OUT_OF_CASES, "%u");
	USFSTL_ASSERT_CMP((int)status, !=, (int)USFSTL_STATUS_SKIPPED, "%u");

	write_requirements(tc, status == USFSTL_STATUS_SUCCESS,
			   status != USFSTL_STATUS_SUCCESS);
	close_requirements();

	return status;
}
#else
int main(int argc, char **argv)
{
	int tcidx, i, test = -1;
	int ctr = 0;
	int ret = 0;
	int succeeded = 0, failed = 0;
	uint64_t tm;

	tm = get_monotonic_time_ms();

	ret = usfstl_init(argc, argv);
	if (ret)
		return ret;

	if (usfstl_is_multi_participant())
		return usfstl_multi_participant_run();

	if (g_usfstl_list_asserts) {
		usfstl_list_all_asserts();
		return 0;
	}

	for (tcidx = 0; &__start_usfstl_tests[tcidx] < &__stop_usfstl_tests; tcidx++) {
		const struct usfstl_test *tc = __start_usfstl_tests[tcidx];
		unsigned int tc_succeeded = 0, tc_failed = 0;

		if (!tc)
			continue;

		test++;

		if (g_usfstl_test >= 0 && test != g_usfstl_test)
			continue;

		if (g_usfstl_skip_known_failing && tc->failing)
			continue;

		for (i = 0; /* until we break */; i++, ctr++) {
			enum usfstl_testcase_status status;
			bool execute = !g_usfstl_count && !g_usfstl_list_tests;

			if (g_usfstl_testcase >= 0) {
				if (g_usfstl_testcase_last < 0 && i != g_usfstl_testcase)
					execute = false;
				if (g_usfstl_testcase_last >= 0 &&
				    (i < g_usfstl_testcase || g_usfstl_testcase_last < i))
					execute = false;
			}

			status = usfstl_execute_test(tc, test, i, execute);

			if (status == USFSTL_STATUS_OUT_OF_CASES)
				break;
			if (!execute || status == USFSTL_STATUS_SKIPPED)
				continue;

			if (status != USFSTL_STATUS_SUCCESS) {
				ret = 1;
				tc_failed++;
			} else {
				tc_succeeded++;
			}
		}

		if (g_usfstl_list_tests) {
			printf("%s: %d\n", tc->name, i);
			continue;
		}

		failed += tc_failed;
		succeeded += tc_succeeded;

		write_summary(tc->name, tc_succeeded, tc_failed);
		write_requirements(tc, tc_succeeded, tc_failed);
	}

	if (g_usfstl_count) {
		printf("%d\n", ctr);
		return 0;
	}

	if (g_usfstl_list_tests)
		return 0;

	usfstl_multi_finish();

	printf("\nRan %d tests, %d failed.\n", succeeded + failed, failed);
	write_summary("TOTAL", succeeded, failed);
	close_summary();
	close_requirements();

	tm = get_monotonic_time_ms() - tm;
	printf("Took %ld.%03ld seconds (wall clock)\n",
	       (unsigned long)(tm / 1000), (unsigned long)(tm % 1000));

	return ret;
}
#endif /* USFSTL_LIBRARY */
