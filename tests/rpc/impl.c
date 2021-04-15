/*
 * Copyright (C) 2019 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "rpc.h"

#define USFSTL_RPC_IMPLEMENTATION
#include <usfstl/rpc.h>

USFSTL_RPC_METHOD(int32_t, callme1, uint32_t)
{
	return in + 42;
}

USFSTL_RPC_METHOD_P(int32_t, callme2, struct foo)
{
	return in->bar + 42;
}

USFSTL_RPC_VOID_METHOD(callme3, uint32_t)
{
	printf("%d (running on %s)\n", in, program_invocation_name + 2);
}

USFSTL_RPC_VOID_METHOD_P(callme4, struct foo)
{
	printf("%d (running on %s)\n", in->bar, program_invocation_name + 2);
}

int32_t exitstatus;

USFSTL_RPC_VOID_METHOD(quit, int32_t)
{
	exitstatus = in;
}

USFSTL_RPC_VOID_METHOD(recurse, int32_t)
{
	printf("recurse %d (running on %s)\n", in, program_invocation_name + 2);
	if (in > 0) {
		printf("> calling recurse(%d)\n", in - 1);
		recurse(in - 1);
		printf("< recurse subcall returned\n");
	}
}

USFSTL_RPC_METHOD_VAR(int32_t, rpclog, struct log)
{
	printf("running on %s, rpclog message: %.*s\n",
	       program_invocation_name + 2,
	       (int32_t)(insize - sizeof(*in)), in->msg);
	return 0;
}

USFSTL_RPC_VAR_METHOD(struct log, numformat, int32_t)
{
	snprintf(out->msg, outsize, "formatted %d!", in);
}

USFSTL_RPC_VAR_METHOD_P(struct log, numformatp, struct foo)
{
	snprintf(out->msg, outsize, "formatted %d (p)!", in->bar);
}

USFSTL_RPC_VAR_METHOD_VAR(struct log, hello, struct log)
{
	printf("running on %s, '%.*s' says hi\n",
	       program_invocation_name + 2,
	       (int32_t)(insize - sizeof(*in)), in->msg);
	snprintf(out->msg, outsize, "Hello %.*s!",
		 (int32_t)(insize - sizeof(*in)), in->msg);
}

/* for rpc flushing - we don't use log stuff here */
void usfstl_flush_all(void)
{
}
