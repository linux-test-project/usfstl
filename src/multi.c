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

void usfstl_multi_start_test(void)
{
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
