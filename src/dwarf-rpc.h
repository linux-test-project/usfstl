/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <usfstl/rpc.h>
#include <stdint.h>
#include "internal.h"

#ifndef __USFSTL_DWARF_RPC_H
#define __USFSTL_DWARF_RPC_H
struct usfstl_bt_name {
	char dummy;
	char name[];
};
#endif // __USFSTL_DWARF_RPC_H

// declare functions outside ifdefs, needed for code generation
// (and doesn't hurt if this gets included twice either, since
// they're just prototypes)

USFSTL_RPC_VOID_METHOD(rpc_bt_start, uint32_t /* dummy */);
USFSTL_RPC_VAR_METHOD(struct usfstl_bt_name, rpc_bt, uint32_t /* dummy */);
