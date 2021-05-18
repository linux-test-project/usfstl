/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <usfstl/rpc.h>
#include <stdint.h>
#include "internal.h"

#ifndef __USFSTL_RPC_RPC_H
#define __USFSTL_RPC_RPC_H
struct usfstl_rpc_init {
	uint32_t extra_len;
} __attribute__((packed));
#endif // __USFSTL_RPC_RPC_H

// declare functions outside ifdefs, needed for code generation
// (and doesn't hurt if this gets included twice either, since
// they're just prototypes)

USFSTL_RPC_VAR_METHOD_VAR(struct usfstl_rpc_init, rpc_init,
			  struct usfstl_rpc_init);
USFSTL_RPC_VOID_METHOD(rpc_disconnect, uint32_t /* ignored */);
