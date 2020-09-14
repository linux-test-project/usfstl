/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * RPC stubs for multi-participant simulation.
 */
#include "multi-rpc.h"

#define USFSTL_RPC_CALLEE_STUB
#include "multi-rpc.h"
#undef USFSTL_RPC_CALLEE_STUB

#define USFSTL_RPC_CALLER_STUB
#include "multi-rpc.h"
#undef USFSTL_RPC_CALLER_STUB
