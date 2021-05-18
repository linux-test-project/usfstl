/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * RPC stubs for RPC itself (initialization)
 */
#include <assert.h>
#include "rpc-rpc.h"

#define USFSTL_RPC_CALLEE_STUB
#include "rpc-rpc.h"
#undef USFSTL_RPC_CALLEE_STUB

#define USFSTL_RPC_CALLER_STUB
#include "rpc-rpc.h"
#undef USFSTL_RPC_CALLER_STUB

#define USFSTL_RPC_IMPLEMENTATION
#include <usfstl/rpc.h>

USFSTL_RPC_VAR_METHOD_VAR(struct usfstl_rpc_init, rpc_init,
			  struct usfstl_rpc_init)
{
	conn->initialized = 1;
	assert(in->extra_len == conn->extra_len);
	out->extra_len = conn->extra_len;
}

USFSTL_RPC_VOID_METHOD(rpc_disconnect, uint32_t /* ignored */)
{
	conn->broken = 1;

	if (conn->disconnected) {
		usfstl_loop_unregister(&conn->conn);
		conn->disconnected(conn);
	}
}
