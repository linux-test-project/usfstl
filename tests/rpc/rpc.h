/*
 * Copyright (C) 2019 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#if !defined(__RPC_H) || defined(USFSTL_RPC_CALLEE_STUB) || defined(USFSTL_RPC_CALLER_STUB)
#define __RPC_H
#include <usfstl/rpc.h>
#include <stdint.h>
#include "struct.h"

USFSTL_RPC_METHOD(int32_t, callme1, uint32_t);
USFSTL_RPC_METHOD_P(int32_t, callme2, struct foo);
USFSTL_RPC_VOID_METHOD(callme3, uint32_t);
USFSTL_RPC_VOID_METHOD_P(callme4, struct foo);
USFSTL_RPC_VOID_METHOD(quit, int32_t);
USFSTL_RPC_VOID_METHOD(recurse, int32_t);
USFSTL_RPC_METHOD_VAR(int32_t, rpclog, struct log);
USFSTL_RPC_VAR_METHOD(struct log, numformat, int32_t);
USFSTL_RPC_VAR_METHOD_P(struct log, numformatp, struct foo);
USFSTL_RPC_VAR_METHOD_VAR(struct log, hello, struct log);

#endif // _RPC_H
