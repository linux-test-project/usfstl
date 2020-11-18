/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <usfstl/rpc.h>
#include <stdint.h>
#include "internal.h"

#ifndef __USFSTL_PRINT_RPC_H
#define __USFSTL_PRINT_RPC_H
struct usfstl_rpc_log_create {
	char name[0];
} __attribute__((packed));
#endif // __USFSTL_PRINT_RPC_H

// declare functions outside ifdefs, needed for code generation
// (and doesn't hurt if this gets included twice either, since
// they're just prototypes)

USFSTL_RPC_METHOD_VAR(int, rpc_log_create, struct usfstl_rpc_log_create);
USFSTL_RPC_VOID_METHOD(rpc_log_close, int);
