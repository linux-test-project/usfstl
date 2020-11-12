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

static bool g_usfstl_debug_subprocesses;
USFSTL_OPT_FLAG("multi-debug-subprocs", 0, g_usfstl_debug_subprocesses,
		"Break into a debugger once all sub-processes are started");

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

static USFSTL_SCHEDULER(g_usfstl_multi_sched);

static void usfstl_multi_ctl_extra_transmit(struct usfstl_rpc_connection *conn,
					    void *data)
{
	struct usfstl_multi_sync *sync = data;

	/*
	 * We're the master, so we just send direct RPC calls to the other
	 * participants, and we need to send it the later time, depending
	 * on what happened - did somebody else run? then the multi-sched
	 * will have the later time, if we ran or are running, the task
	 * scheduler has it.
	 */

	if (usfstl_time_cmp(g_usfstl_task_scheduler.current_time,
			    >,
			    g_usfstl_multi_sched.current_time))
		sync->time = g_usfstl_task_scheduler.current_time;
	else
		sync->time = g_usfstl_multi_sched.current_time;
}

static void usfstl_multi_ctl_start_participant(struct usfstl_multi_participant *p)
{
	int nargs = 0;

	if (p->pre_connected)
		goto setup;

	assert(p->binary);

	while (p->args && p->args[nargs])
		nargs++;

	usfstl_run_participant(p, nargs);
setup:
	p->conn->data = p;
	p->conn->extra_len = sizeof(struct usfstl_multi_sync);
	p->conn->extra_transmit = usfstl_multi_ctl_extra_transmit;
	p->conn->extra_received = usfstl_multi_extra_received;
	usfstl_rpc_add_connection(p->conn);
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

		for_each_participant(p, i)
			printf("# %s:\ngdb -p %u\n\n", p->name, p->pid);

		// debug trap - we only support x86 anyway right now
		__asm__ __volatile__("int $3");
	}

	g_usfstl_top_scheduler = &g_usfstl_multi_sched;
}

static void usfstl_multi_controller_wait_all(uint32_t flag, bool schedule)
{
	while (1) {
		struct usfstl_multi_participant *p;
		bool all = true;
		int i;

		for_each_participant(p, i) {
			if (!(p->flags & flag)) {
				all = false;
				break;
			}
		}

		if (all)
			break;

		usfstl_rpc_handle();
		/*
		 * While we're shutting down (only place setting schedule=true)
		 * we need to still continue scheduling since the other (remote)
		 * participants in the simulation may request runtime.
		 */
		if (schedule && !usfstl_list_empty(&g_usfstl_multi_sched.joblist))
			usfstl_sched_next(&g_usfstl_multi_sched);
	}
}

static void
usfstl_multi_controller_update_sync_time(struct usfstl_multi_participant *update)
{
	uint64_t time = usfstl_sched_current_time(&g_usfstl_multi_sched);
	// pick something FAR away (but not considered in the past)
	// so we don't sync often if nothing really happens
	uint64_t sync = time + (1ULL << 62);
	struct usfstl_job *job;

	job = usfstl_sched_next_pending(&g_usfstl_multi_sched, NULL);
	if (job)
		sync = job->start;

	if (!update)
		update = g_usfstl_multi_running_participant;
	else
		g_usfstl_multi_running_participant = update;

	// If we synced it to exactly the same time before, don't do it again.
	if (update->sync_set && update->sync == sync)
		return;

	multi_rpc_sched_set_sync_conn(update->conn, sync);
	update->sync_set = 1;
	update->sync = sync;
}

static void usfstl_multi_controller_task_callback(struct usfstl_job *job)
{
	usfstl_sched_set_sync_time(&g_usfstl_task_scheduler, job->start);
}

static struct usfstl_job g_usfstl_multi_ctl_task_job = {
	.name = "local-tasks",
	.callback = usfstl_multi_controller_task_callback,
};

static void usfstl_multi_sched_ext_req_controller(struct usfstl_scheduler *sched,
						  uint64_t at)
{
	usfstl_sched_del_job(&g_usfstl_multi_ctl_task_job);
	g_usfstl_multi_ctl_task_job.start = at;
	usfstl_sched_add_job(&g_usfstl_multi_sched,
			     &g_usfstl_multi_ctl_task_job);

	usfstl_multi_controller_update_sync_time(NULL);
}

static void usfstl_multi_sched_ext_wait_controller(struct usfstl_scheduler *sched)
{
	struct usfstl_job *ran;

	// save the local view of the shared memory before waiting
	usfstl_shared_mem_prepare_msg(true);

	do {
		usfstl_multi_controller_wait_all(USFSTL_MULTI_PARTICIPANT_WAITING, false);
		ran = usfstl_sched_next(&g_usfstl_multi_sched);
	} while (ran != &g_usfstl_multi_ctl_task_job);

	usfstl_sched_set_time(&g_usfstl_task_scheduler,
			      usfstl_sched_current_time(&g_usfstl_multi_sched));

	// refresh the local view of the shared memory before continuing
	usfstl_shared_mem_update_local_view();
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

	g_usfstl_multi_test_running = true;

	for_each_participant(p, i)
		multi_rpc_test_start_conn(p->conn, &msg.hdr,
					  sizeof(msg.hdr) + strlen(msg.name));

	usfstl_multi_controller_wait_all(USFSTL_MULTI_PARTICIPANT_STARTED, false);

	// local scheduler also needs to integrate, set that up here
	// to avoid reset during globals restore
	g_usfstl_task_scheduler.external_request =
		usfstl_multi_sched_ext_req_controller;
	g_usfstl_task_scheduler.external_wait =
		usfstl_multi_sched_ext_wait_controller;
}

void usfstl_multi_end_test_controller(enum usfstl_testcase_status status)
{
	struct usfstl_multi_participant *p;
	int i;

	/*
	 * Sync task scheduler time to local time, so we can safely
	 * tell all the others - and if they request some runtime
	 * they'll do it at the right time.
	 * This is the only place we need to do it because when any
	 * other component gets a future sync point, it will still
	 * have to schedule again, and we also sync on that.
	 */
	_usfstl_sched_set_time(&g_usfstl_multi_sched,
			     usfstl_sched_current_time(&g_usfstl_task_scheduler));

	for_each_participant(p, i)
		multi_rpc_test_end_conn(p->conn, status);

	g_usfstl_multi_test_running = false;

	usfstl_multi_controller_wait_all(USFSTL_MULTI_PARTICIPANT_FINISHED, true);
}

void usfstl_multi_finish(void)
{
	struct usfstl_multi_participant *p;
	int i;

	for_each_participant(p, i) {
		usfstl_rpc_del_connection(p->conn);
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

	// send the updated view of the shared memory (include the buffer
	// only if it has changed)
	multi_rpc_sched_cont_conn(p->conn, g_usfstl_shared_mem_msg,
				  usfstl_shared_mem_get_msg_size(
					p->flags &
					USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED));
	p->flags &= ~USFSTL_MULTI_PARTICIPANT_SHARED_MEM_OUTDATED;
}

#define USFSTL_RPC_IMPLEMENTATION
#include <usfstl/rpc.h>

USFSTL_RPC_VOID_METHOD(multi_rpc_test_started, uint32_t /* dummy */)
{
	struct usfstl_multi_participant *p = conn->data;

	p->flags |= USFSTL_MULTI_PARTICIPANT_STARTED;
}

USFSTL_RPC_VOID_METHOD(multi_rpc_sched_request, uint64_t /* time */)
{
	struct usfstl_multi_participant *p = conn->data;

	/*
	 * If it requests runtime, it also started enough to be scheduled,
	 * it might need to actually schedule before finishing the test's
	 * pre() function.
	 */
	p->flags |= USFSTL_MULTI_PARTICIPANT_STARTED;

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

	usfstl_shared_mem_handle_msg(in, insize);

	// set the flag after handling the shared mem msg, so the handler
	// knows which participant was running
	p->flags |= USFSTL_MULTI_PARTICIPANT_WAITING;

	return 0;
}

USFSTL_RPC_ASYNC_METHOD(multi_rpc_test_failed, uint32_t /* status */)
{
	if (g_usfstl_test_aborted)
		return;

	g_usfstl_failure_reason = in;
	g_usfstl_test_aborted = true;
	usfstl_ctx_abort_test();
}

USFSTL_RPC_VOID_METHOD(multi_rpc_test_ended, uint32_t /* dummy */)
{
	struct usfstl_multi_participant *p = conn->data;

	p->flags |= USFSTL_MULTI_PARTICIPANT_FINISHED;
}
