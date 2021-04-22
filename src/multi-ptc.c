/*
 * Copyright (C) 2019 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Participant code for multi-participant simulation.
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <usfstl/test.h>
#include <usfstl/rpc.h>
#include <usfstl/multi.h>
#include <usfstl/sched.h>
#include <usfstl/task.h>
#include "internal.h"
#include "multi-rpc.h"

// variables for participant
struct usfstl_test USFSTL_NORESTORE_VAR(g_usfstl_multi_controlled_test);
static bool g_usfstl_multi_test_sched_continue;
static bool USFSTL_NORESTORE_VAR(g_usfstl_ptc_must_send_test_end_response);
static bool g_usfstl_multi_ptc_remote_abort;

bool USFSTL_NORESTORE_VAR(g_usfstl_multi_test_participant);

static bool usfstl_set_multi_participant_name(struct usfstl_opt *opt,
					      const char *arg)
{
	g_usfstl_multi_test_participant = true;
	g_usfstl_multi_local_participant.name = arg;

	return true;
}

/*
 * Set the participant name in g_usfstl_multi_local_participant.name
 * (because that's also set by the user for the multi controller for
 * logging purposes) - we print that on test failures (if not NULL).
 */
USFSTL_OPT("multi-ptc-name", 0, "name",
	   usfstl_set_multi_participant_name, NULL,
	   "Participant name, set by the controller");

static void usfstl_multi_participant_test_fn(const struct usfstl_test *test, void *tc)
{
	/*
	 * Do nothing. We never return from this, we longjmp() directly back
	 * to usfstl_execute_test() when the test completes, regardless of
	 * its outcome.
	 */
	usfstl_task_suspend();

	USFSTL_ASSERT(0, "test task in participant should never resume");
}

static void usfstl_multi_sched_ext_req(struct usfstl_scheduler *sched,
				       uint64_t at)
{
	multi_rpc_sched_request_conn(g_usfstl_multi_ctrl_conn, at);
}

static void usfstl_multi_sched_ext_wait_participant(struct usfstl_scheduler *sched)
{
	g_usfstl_multi_test_sched_continue = false;

	// save the local view of the shared memory before waiting
	usfstl_shared_mem_prepare_msg(false);

	// send the updated view of the shared memory (include the buffer
	// only if it has changed)
	multi_rpc_sched_wait_conn(g_usfstl_multi_ctrl_conn,
				  g_usfstl_shared_mem_msg,
				  usfstl_shared_mem_get_msg_size(
					g_usfstl_shared_mem_dirty));
	g_usfstl_shared_mem_dirty = false;

	while (!g_usfstl_multi_test_sched_continue)
		usfstl_rpc_handle();

	usfstl_shared_mem_update_local_view();
}

void usfstl_multi_start_test_participant(void)
{
	g_usfstl_test_aborted = false;

	g_usfstl_multi_sched.external_request = usfstl_multi_sched_ext_req;
	g_usfstl_multi_sched.external_wait =
		usfstl_multi_sched_ext_wait_participant;
}

void usfstl_multi_end_test_participant(void)
{
	if (g_usfstl_ptc_must_send_test_end_response) {
		g_usfstl_ptc_must_send_test_end_response = false;
		usfstl_rpc_send_void_response(g_usfstl_multi_ctrl_conn);
	}
}

int usfstl_multi_participant_run(void)
{
	usfstl_multi_add_rpc_connection(g_usfstl_multi_ctrl_conn);

	while (g_usfstl_multi_ctrl_conn) {
		enum usfstl_testcase_status status;

		USFSTL_ASSERT(!g_usfstl_multi_controlled_test.name,
			      "participant has test name before starting");

		/*
		 * Initially, we just wait for a single to do anything,
		 * and loop over usfstl_rpc_handle() here.
		 */
		usfstl_rpc_handle();

		/* As soon as we have a test case to run ... */
		if (!g_usfstl_multi_controlled_test.name)
			continue;

		/*
		 * We actually execute it. This looks just like a normal
		 * test execution because it mostly is, except that the
		 * task scheduler's external request/wait callbacks are
		 * now set, and they will continue the RPC process. In
		 * particular, the external wait will just loop to handle
		 * RPC messages until it gets runtime.
		 */
		status = usfstl_execute_test(&g_usfstl_multi_controlled_test,
					     g_usfstl_current_test_num,
					     g_usfstl_current_case_num,
					     true);

		free((void *)g_usfstl_multi_controlled_test.name);
		g_usfstl_multi_controlled_test.name = NULL;

		/*
		 * The only other special thing we have to do is tell
		 * the controller if we actually failed the test. We
		 * cannot abort successfully early due to the way we
		 * handle RPC in test execution.
		 */
		if (!g_usfstl_multi_ptc_remote_abort)
			multi_rpc_test_failed_conn(g_usfstl_multi_ctrl_conn, status);
	}

	return 0;
}

#define USFSTL_RPC_IMPLEMENTATION
#include <usfstl/rpc.h>

USFSTL_RPC_METHOD_VAR(uint32_t /* dummy */,
		      multi_rpc_test_start,
		      struct usfstl_multi_run)
{
	struct usfstl_test *test = &g_usfstl_multi_controlled_test;
	char *name;

	test->fn = usfstl_multi_participant_test_fn;
	// test->extra_data is allowed locally

	// there's no strndup on Windows?
	name = malloc(insize - sizeof(in) + 1);
	name[insize - sizeof(*in)] = 0;
	memcpy(name, in->name, insize - sizeof(*in));
	test->name = name;

	test->testcases = NULL;
	test->case_generator = NULL;
	test->testcase_size = 0;
	test->testcase_count = 0;
	test->testcase_generic_offset = 0;
	test->failing = false;
	test->flow_test = in->flow_test;
	test->negative_data = NULL;
	test->max_cpu_time_ms = in->max_cpu_time_ms;
	// test->pre is allowed locally
	// test->post is allowed locally
	test->requirements = NULL;

	g_usfstl_current_test = &g_usfstl_multi_controlled_test;
	// use the variables to keep track of the state,
	// we pass them to usfstl_execute_test() later
	g_usfstl_current_test_num = in->test_num;
	g_usfstl_current_case_num = in->case_num;

	return 0;
}

USFSTL_RPC_VOID_METHOD(multi_rpc_test_end, uint32_t /* status */)
{
	if (!g_usfstl_current_test)
		return;

	g_usfstl_multi_ptc_remote_abort = true;
	g_usfstl_multi_test_sched_continue = true;
	g_usfstl_failure_reason = in ?: USFSTL_STATUS_REMOTE_SUCCESS;

	/*
	 * Always unwind our entire stack by directly swapping
	 * back to usfstl_execute_test() (though we may have to
	 * jump through context swapping) to avoid any more RPC
	 * for scheduling, which just causes trouble with time.
	 */
	g_usfstl_test_aborted = true;
	g_usfstl_ptc_must_send_test_end_response = true;
	usfstl_ctx_abort_test();
}

USFSTL_RPC_VOID_METHOD(multi_rpc_exit, uint32_t /* dummy */)
{
	assert(!g_usfstl_current_test);
	g_usfstl_multi_ctrl_conn = NULL;
	usfstl_multi_finish();
}

USFSTL_RPC_METHOD_VAR(uint32_t /* dummy */,
		      multi_rpc_sched_cont,
		      struct usfstl_shared_mem_msg)
{
	usfstl_shared_mem_handle_msg(in, insize);

	g_usfstl_multi_test_sched_continue = true;

	return 0;
}

USFSTL_RPC_VOID_METHOD(multi_rpc_sched_set_sync, uint64_t /* time */)
{
	usfstl_sched_set_sync_time(&g_usfstl_multi_sched, in);
	usfstl_multi_controller_update_sync_time(NULL);
}

USFSTL_RPC_VOID_METHOD(usfstl_multi_rpc_print_participants, int /* indent */)
{
	usfstl_multi_controller_print_participants(in);
}
