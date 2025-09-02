/*
 * Copyright (C) 2019 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Controller-side code for multi-participant simulation.
 *
 * This is the controller code. Note that the controller may be co-located with
 * the test execution & control and the test code itself, so some code here like
 * the usfstl_multi_sched_ext_wait_controller() function needs to be aware of
 * both.
 */
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <usfstl/test.h>
#include <usfstl/rpc.h>
#include <usfstl/multi.h>
#include <usfstl/sched.h>
#include <usfstl/task.h>
#include "internal.h"
#include "multi-rpc.h"

bool USFSTL_NORESTORE_VAR(g_usfstl_multi_test_controller);
static struct usfstl_multi_participant *
USFSTL_NORESTORE_VAR(g_usfstl_test_fail_initiator);

static bool g_usfstl_debug_subprocesses;
USFSTL_OPT_FLAG("multi-debug-subprocs", 0, g_usfstl_debug_subprocesses,
		"Break into a debugger once all sub-processes are started");

bool USFSTL_NORESTORE_VAR(g_usfstl_multi_ctrl_disable_sync);

/* variables for controller */
// make the section exist even in non-multi builds
static const struct usfstl_multi_participant * const usfstl_multi_participant_NULL
	__attribute__((used, section("usfstl_rpcp"))) = NULL;

struct usfstl_multi_participant g_usfstl_multi_local_participant = {
	.name = "local",
	.conn = USFSTL_RPC_LOCAL,
};
static struct usfstl_multi_participant *g_usfstl_multi_running_participant =
	&g_usfstl_multi_local_participant;

static void usfstl_multi_ctl_start_participant(struct usfstl_multi_participant *p)
{
	int nargs = 0;

	if (p->pre_connected)
		goto setup;

	while (p->args && p->args[nargs])
		nargs++;

	usfstl_run_participant(p, nargs);
setup:
	p->conn->data = p;
	p->conn->name = p->name;
	usfstl_multi_add_rpc_connection(p->conn);
}

void usfstl_multi_controller_print_participants(int indent)
{
	struct usfstl_multi_participant *p;
	int i;

	for_each_participant(p, i) {
		printf("%-*s# %s:\n%-*sgdb -p %u\n\n", indent, "", p->name, indent, "", p->pid);

		usfstl_multi_rpc_print_participants_conn(p->conn, indent + 2);
	}
}

void usfstl_multi_controller_init(void)
{
	struct usfstl_multi_participant *p;
	int i;

	g_usfstl_multi_test_controller = true;

	for_each_participant(p, i)
		usfstl_multi_ctl_start_participant(p);

	if (g_usfstl_debug_subprocesses) {
		printf("\nThe following subprocesses were started, you can attach as follows:\n\n");

		usfstl_multi_controller_print_participants(0);

		// debug trap - we only support x86 anyway right now
		__asm__ __volatile__("int $3");
	}
}

static void usfstl_multi_ctl_wait(struct usfstl_multi_participant *p)
{
	while (!(p->flags & USFSTL_MULTI_PARTICIPANT_WAITING))
		usfstl_rpc_handle();
}

void
usfstl_multi_controller_update_sync_time(struct usfstl_multi_participant *update)
{
	uint64_t sync;

	if (g_usfstl_multi_ctrl_disable_sync)
		return;

	sync = usfstl_sched_get_sync_time(&g_usfstl_multi_sched);

	if (!update)
		update = g_usfstl_multi_running_participant;
	else
		g_usfstl_multi_running_participant = update;

	// If we synced it to exactly the same time before, don't do it again.
	if (update->sync_set && update->sync == sync)
		return;

	if (update == &g_usfstl_multi_local_participant)
		return;

	multi_rpc_sched_set_sync_conn(update->conn, sync);
	update->sync_set = 1;
	update->sync = sync;
}

static void usfstl_multi_ctrl_next_time_changed(struct usfstl_scheduler *sched)
{
	usfstl_multi_controller_update_sync_time(NULL);
}

void usfstl_multi_start_test_controller(void)
{
	struct usfstl_multi_participant *p;
	struct {
		struct usfstl_multi_run hdr;
		char name[1000];
	} msg;
	int i;

	strcpy(msg.name, g_usfstl_current_test->name);
	msg.hdr.test_num = g_usfstl_current_test_num;
	msg.hdr.case_num = g_usfstl_current_case_num;
	msg.hdr.flow_test = g_usfstl_current_test->flow_test;
	msg.hdr.max_cpu_time_ms = g_usfstl_current_test->max_cpu_time_ms;

	for_each_participant(p, i) {
		multi_rpc_test_start_conn(p->conn, &msg.hdr,
					  sizeof(msg.hdr) + strlen(msg.name));
		usfstl_multi_ctl_wait(p);
	}

	g_usfstl_multi_sched.next_time_changed =
		usfstl_multi_ctrl_next_time_changed;
}

void usfstl_multi_end_test_controller(enum usfstl_testcase_status status)
{
	struct usfstl_multi_participant *p;
	int i;

	for_each_participant(p, i) {
		if (p == g_usfstl_test_fail_initiator)
			continue;
		multi_rpc_test_end_conn(p->conn, status);
	}

	if (g_usfstl_test_fail_initiator) {
		usfstl_rpc_send_void_response(g_usfstl_test_fail_initiator->conn);
		g_usfstl_test_fail_initiator = NULL;
	}
}

void usfstl_multi_finish(void)
{
	struct usfstl_multi_participant *p;
	int i;

	for_each_participant(p, i) {
		usfstl_rpc_del_connection_raw(p->conn);
		multi_rpc_exit_conn(p->conn, 0);
	}
}

static void usfstl_multi_controller_sched_callback(struct usfstl_job *job)
{
	struct usfstl_multi_participant *p = job->data;

	p->flags &= ~USFSTL_MULTI_PARTICIPANT_WAITING;

	// We're letting this participant run, so update its idea
	// of how long it's allowed to run.
	usfstl_multi_controller_update_sync_time(p);

	// save the local view of the shared memory before waiting
	usfstl_shared_mem_prepare_msg();

	// send the updated view of the shared memory (include the buffer
	// only if it has changed)
	multi_rpc_sched_cont_conn(p->conn, &g_usfstl_sched_req_and_wait_msg->shared_mem,
				  usfstl_shared_mem_get_msg_size(
					p->flags &
					USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED));
	p->flags &= ~USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED;

	usfstl_multi_ctl_wait(p);

	// refresh the local view of the shared memory before continuing
	usfstl_shared_mem_update_local_view();
}

#define USFSTL_RPC_IMPLEMENTATION
#include <usfstl/rpc.h>

USFSTL_RPC_VOID_METHOD(multi_rpc_sched_request, uint64_t /* time */)
{
	struct usfstl_multi_participant *p = conn->data;

	usfstl_sched_del_job(&p->job);
	p->job.name = p->name;
	p->job.start = in;
	p->job.data = p;
	p->job.callback = usfstl_multi_controller_sched_callback;

	usfstl_sched_add_job(&g_usfstl_multi_sched, &p->job);

	usfstl_multi_controller_update_sync_time(NULL);
}

USFSTL_RPC_METHOD_VAR(uint32_t /* dummy */,
		      multi_rpc_sched_wait,
		      struct usfstl_shared_mem_msg)
{
	struct usfstl_multi_participant *p = conn->data;

	usfstl_shared_mem_handle_msg(in, insize, false);
	usfstl_shared_mem_update_local_view();

	// set the flag after handling the shared mem msg, so the handler
	// knows which participant was running
	p->flags |= USFSTL_MULTI_PARTICIPANT_WAITING;

	return 0;
}

USFSTL_RPC_METHOD_VAR(uint32_t /* dummy */,
		      multi_rpc_sched_req_and_wait,
		      struct usfstl_sched_req_and_wait_msg)
{
	uint32_t shared_mem_size = insize - offsetof(typeof(*in), shared_mem);

	_impl_multi_rpc_sched_request(conn, in->time);
	return _impl_multi_rpc_sched_wait(conn, &in->shared_mem, shared_mem_size);
}

USFSTL_RPC_VOID_METHOD(multi_rpc_test_failed, uint32_t /* status */)
{
	if (g_usfstl_test_aborted)
		return;

	g_usfstl_failure_reason = in;
	g_usfstl_test_aborted = true;
	g_usfstl_test_fail_initiator = conn->data;
	usfstl_ctx_abort_test();
}
