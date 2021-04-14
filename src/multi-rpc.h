/*
 * Copyright (C) 2019 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <usfstl/rpc.h>
#include <usfstl/sharedmem.h>
#include <stdint.h>
#include "internal.h"

#ifndef __USFSTL_MULTI_RPC_H
#define __USFSTL_MULTI_RPC_H
struct usfstl_multi_sync {
	uint64_t time;
};

struct usfstl_multi_run {
	uint32_t test_num, case_num;
	uint32_t max_cpu_time_ms;
	unsigned char flow_test;
	char name[0];
} __attribute__((packed));

struct usfstl_shared_mem_msg_section {
	usfstl_shared_mem_section_name_t name;
	uint32_t size;	/* buffer size */
	char buf[0];
} __attribute__((packed));

struct usfstl_shared_mem_msg {
	struct usfstl_shared_mem_msg_section sections[0];
} __attribute__((packed));
#endif // __USFSTL_MULTI_RPC_H

// declare functions outside ifdefs, needed for code generation
// (and doesn't hurt if this gets included twice either, since
// they're just prototypes)

/* controller -> participant */
USFSTL_RPC_METHOD_VAR(uint32_t /* dummy */,
		      multi_rpc_test_start,
		      struct usfstl_multi_run);
USFSTL_RPC_ASYNC_METHOD(multi_rpc_test_end, uint32_t /* status */);
USFSTL_RPC_VOID_METHOD(multi_rpc_exit, uint32_t /* dummy */);
USFSTL_RPC_METHOD_VAR(uint32_t /* dummy */,
		      multi_rpc_sched_cont,
		      struct usfstl_shared_mem_msg);
USFSTL_RPC_VOID_METHOD(multi_rpc_sched_set_sync, uint64_t /* time */);

/* participant -> controller */
USFSTL_RPC_VOID_METHOD(multi_rpc_sched_request, uint64_t /* time */);
USFSTL_RPC_METHOD_VAR(uint32_t /* dummy */,
		      multi_rpc_sched_wait,
		      struct usfstl_shared_mem_msg);
USFSTL_RPC_ASYNC_METHOD(multi_rpc_test_failed, uint32_t /* status */);
USFSTL_RPC_VOID_METHOD(multi_rpc_test_ended, uint32_t /* dummy */);
