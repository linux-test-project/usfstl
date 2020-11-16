/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <usfstl/test.h>
#include <usfstl/ctx.h>
#include <usfstl/log.h>
#include <usfstl/multi.h>
#include "internal.h"

static jmp_buf g_usfstl_jmp_buf;
bool g_usfstl_abort_on_error;
bool g_usfstl_disable_wdt;
bool g_usfstl_test_aborted;
const struct usfstl_test *g_usfstl_current_test;
struct usfstl_testcase *g_usfstl_current_testcase;
void *g_usfstl_current_test_case_data;
int g_usfstl_current_case_num = -1;
int g_usfstl_current_test_num = -1;
unsigned int g_usfstl_failure_reason;
usfstl_abort_handler_t g_usfstl_abort_handler;

void usfstl_out_of_time(void *rip)
{
	char funcname[1000] = {}, filename[1000] = {};
	unsigned int line;

	usfstl_get_function_info(rip, funcname, filename, &line);

	usfstl_printf("\n!!!! test timeout in %s() at %s:%d !!!!\n",
		      funcname, filename, line);
	/* we printed that to stdout, flush */
	fflush(stdout);

	if (g_usfstl_abort_on_error)
		abort();
	if (usfstl_ctx_is_main())
		longjmp(g_usfstl_jmp_buf, USFSTL_STATUS_WATCHDOG_TIMEOUT);
	g_usfstl_test_aborted = true;
	g_usfstl_failure_reason = USFSTL_STATUS_WATCHDOG_TIMEOUT;
	usfstl_ctx_abort_test();
}

void usfstl_complete_abort(void)
{
	longjmp(g_usfstl_jmp_buf, g_usfstl_failure_reason);
}

void usfstl_abort(const char *fn, unsigned int line, const char *cond,
		  const char *msg, ...)
{
	va_list va;

	if (g_usfstl_abort_handler) {
		usfstl_printf("Calling user-defined abort handler\n");
		fflush(stdout);
		g_usfstl_abort_handler(fn, line, cond);
	}

	usfstl_flush_all();

	usfstl_printf("\n");
	usfstl_printf("!!!! assertion failure in %s:%d", fn, line);

	if (g_usfstl_current_case_num >= 0) {
		usfstl_printf(" (test '%s', case #%d)\n",
			      g_usfstl_current_test->name, g_usfstl_current_case_num);
	} else {
		usfstl_printf("\n");
	}

	if (g_usfstl_current_testcase && g_usfstl_current_testcase->name)
		usfstl_printf("  testcase name:\n\t\"\"\"%s\"\"\"\n\n",
			      g_usfstl_current_testcase->name);

	if (g_usfstl_current_case_num >= 0) {
		usfstl_printf("\t\tre-run just this test with '--test=%d --case=%d'\n",
			      g_usfstl_current_test_num, g_usfstl_current_case_num);
	}

	if (g_usfstl_multi_local_participant.name)
		usfstl_printf("!!!! component %s\n", g_usfstl_multi_local_participant.name);

	usfstl_printf("\n");
	usfstl_printf("  %s\n", cond);
	usfstl_printf("\n");
	va_start(va, msg);
	usfstl_vprintf(msg, va);
	va_end(va);
	usfstl_printf("\n");

	if (g_usfstl_current_testcase && g_usfstl_current_testcase->failing)
		usfstl_printf("!!!! NOTE: This failure is a know one (marked explicitly in the testcase)\n\n");

	/* we printed all that to stdout, flush */
	fflush(stdout);
	/* so it doesn't interleave with this on stderr */
	_usfstl_dump_stack(1);
	fflush(stderr);

#ifdef USFSTL_USE_FUZZING
	abort();
#endif

	if (g_usfstl_abort_on_error || g_usfstl_current_case_num < 0)
		abort();

	g_usfstl_test_aborted = true;
	g_usfstl_failure_reason = USFSTL_STATUS_ASSERTION_FAILED;
	usfstl_ctx_abort_test();

	/* this cannot happen, but make __attribute__((noreturn)) happy */
	longjmp(g_usfstl_jmp_buf, USFSTL_STATUS_ASSERTION_FAILED);
}

enum usfstl_testcase_status usfstl_execute_test(const struct usfstl_test *test,
						unsigned int test_num,
						unsigned int case_num,
						bool execute)
{
	enum usfstl_testcase_status status;
	char dummy;

	if (execute) {
		usfstl_reset_overrides();
		usfstl_restore_globals();
	}

	g_usfstl_current_test = test;
	g_usfstl_current_case_num = case_num;
	g_usfstl_current_test_num = test_num;
	g_usfstl_test_aborted = false;
	g_usfstl_current_testcase = NULL;

	if (test == &g_usfstl_multi_controlled_test) {
		g_usfstl_current_test_case_data = NULL;
	} else if (test->testcase_count) {
		if (case_num >= test->testcase_count) {
			status = USFSTL_STATUS_OUT_OF_CASES;
			goto out;
		}
		g_usfstl_current_test_case_data = (char *)test->testcases + case_num * test->testcase_size;
		g_usfstl_current_testcase = (void *)((char *)g_usfstl_current_test_case_data +
						     test->testcase_generic_offset);
		if (g_usfstl_skip_known_failing &&
		    g_usfstl_current_testcase->failing) {
			status = USFSTL_STATUS_SKIPPED;
			goto out;
		}
	} else if (test->case_generator) {
		g_usfstl_current_test_case_data =
			test->case_generator(test, case_num);
		if (!g_usfstl_current_test_case_data) {
			status = USFSTL_STATUS_OUT_OF_CASES;
			goto out;
		}

		if (test->case_generator_has_generic) {
			g_usfstl_current_testcase = (void *)
				((char *)g_usfstl_current_test_case_data +
					 test->testcase_generic_offset);
			if (g_usfstl_skip_known_failing &&
			    g_usfstl_current_testcase->failing) {
				status = USFSTL_STATUS_SKIPPED;
				goto out;
			}
		}
	} else if (case_num > 0) {
		status = USFSTL_STATUS_OUT_OF_CASES;
		goto out;
	}

	if (!execute) {
		status = USFSTL_STATUS_SUCCESS;
		goto out;
	}

	usfstl_set_stack_start(&dummy);

	if (!g_usfstl_disable_wdt && test->max_cpu_time_ms)
		usfstl_watchdog_start(test->max_cpu_time_ms);

	USFSTL_BUILD_BUG_ON(USFSTL_STATUS_SUCCESS != 0);

	status = setjmp(g_usfstl_jmp_buf);

	switch (status) {
	case USFSTL_STATUS_SUCCESS:
		usfstl_multi_start_test();

		if (test->pre)
			test->pre(g_usfstl_current_test, g_usfstl_current_test_case_data,
				  test_num, case_num);

		test->fn(g_usfstl_current_test, g_usfstl_current_test_case_data);

		if (test->negative_data ||
		    (g_usfstl_current_testcase &&
		     g_usfstl_current_testcase->negative_data)) {
			if (g_usfstl_abort_on_error)
				abort();
			status = USFSTL_STATUS_NEGATIVE_TEST_NO_ERROR;
			break;
		}
		break;
	case USFSTL_STATUS_REMOTE_SUCCESS:
	case USFSTL_STATUS_NEGATIVE_TEST_SUCCEEDED:
		/* negative test or remote success */
		status = USFSTL_STATUS_SUCCESS;
		break;
	case USFSTL_STATUS_WATCHDOG_TIMEOUT:
	case USFSTL_STATUS_ASSERTION_FAILED:
		break;
	default:
		USFSTL_ASSERT(0, "test run ended with bad status %d\n", status);
	}

	if (!g_usfstl_disable_wdt && test->max_cpu_time_ms)
		usfstl_watchdog_stop();

        if (g_usfstl_assert_coverage_file)
                usfstl_log_reached_asserts();

	if (test->post)
		test->post(g_usfstl_current_test, g_usfstl_current_test_case_data,
			   test_num, case_num, status);

	/*
	 * If this was a fuzz test running in AFL then we can just exit(0) here
	 * to speed up the testing a bit - nothing that comes after this will
	 * materially affect the outcome of the test.
	 */
	usfstl_fuzz_test_ok();

	usfstl_multi_end_test(status);

	usfstl_task_cleanup();
	usfstl_ctx_cleanup();
	usfstl_free_all();

	if (g_usfstl_current_testcase && g_usfstl_current_testcase->requirement)
		usfstl_tested_requirement(g_usfstl_current_testcase->requirement,
					  status == USFSTL_STATUS_SUCCESS);

out:
	g_usfstl_current_case_num = -1;
	g_usfstl_current_test_num = -1;
	g_usfstl_current_test_case_data = NULL;
	return status;
}

void usfstl_negative_test_succeeded(void)
{
	g_usfstl_test_aborted = true;
	g_usfstl_failure_reason = USFSTL_STATUS_NEGATIVE_TEST_SUCCEEDED;
	usfstl_ctx_abort_test();
}
