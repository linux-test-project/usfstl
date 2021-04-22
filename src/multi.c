/*
 * Copyright (C) 2019 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Common code for multi-participant simulation
 */
#include <stdio.h>
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

USFSTL_SCHEDULER(g_usfstl_multi_sched);

// connection to the controller, set to USFSTL_RPC_LOCAL for itself
// (so it can treat itself as a normal participant in most places)
struct usfstl_rpc_connection *USFSTL_NORESTORE_VAR(g_usfstl_multi_ctrl_conn);

void usfstl_multi_init(void)
{
	struct usfstl_multi_participant *p;
	int i;

	/* if there's any participant, init as controller */
	for_each_participant(p, i) {
		usfstl_multi_controller_init();
		break;
	}
}

static void usfstl_multi_extra_transmit(struct usfstl_rpc_connection *conn,
					void *data)
{
	struct usfstl_scheduler *scheduler;
	struct usfstl_multi_sync *sync = data;

	scheduler = conn->conn.data ?: g_usfstl_top_scheduler;

	sync->time = usfstl_sched_current_time(scheduler);
}

static void usfstl_multi_extra_received(struct usfstl_rpc_connection *conn,
					const void *data)
{
	struct usfstl_scheduler *scheduler;
	const struct usfstl_multi_sync *sync = data;

	if (!g_usfstl_current_test)
		return;

	scheduler = conn->conn.data ?: g_usfstl_top_scheduler;

	if (usfstl_sched_current_time(scheduler) != sync->time)
		usfstl_sched_set_time(scheduler, sync->time);
}

void usfstl_multi_add_rpc_connection_sched(struct usfstl_rpc_connection *conn,
					   struct usfstl_scheduler *sched)
{
	conn->extra_len = sizeof(struct usfstl_multi_sync);
	conn->extra_transmit = usfstl_multi_extra_transmit;
	conn->extra_received = usfstl_multi_extra_received;

	// use conn.data to point to the scheduler to use
	USFSTL_ASSERT(!conn->conn.data);
	conn->conn.data = sched;

	usfstl_rpc_add_connection(conn);
}

void usfstl_multi_start_test(void)
{
	// ensure there won't be an unexpected offset
	USFSTL_ASSERT_EQ(usfstl_sched_current_time(&g_usfstl_task_scheduler),
			 (uint64_t)0ULL, "%"PRIu64);
	USFSTL_ASSERT_EQ(usfstl_sched_current_time(&g_usfstl_multi_sched),
			 (uint64_t)0ULL, "%"PRIu64);

	if (usfstl_is_multi_controller() || usfstl_is_multi_participant()) {
		usfstl_sched_link(&g_usfstl_task_scheduler,
				  &g_usfstl_multi_sched, 1);

		g_usfstl_top_scheduler = &g_usfstl_multi_sched;
	}

	if (usfstl_is_multi_controller())
		usfstl_multi_start_test_controller();

	if (usfstl_is_multi_participant())
		usfstl_multi_start_test_participant();
}

void usfstl_multi_end_test(enum usfstl_testcase_status status)
{
	if (usfstl_is_multi_controller())
		usfstl_multi_end_test_controller(status);

	if (usfstl_is_multi_participant())
		usfstl_multi_end_test_participant();
}
