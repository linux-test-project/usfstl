/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "rpc.h"

extern void calls(void);

static uint32_t extra_value;

static void extra_transmit(struct usfstl_rpc_connection *conn, void *data)
{
	uint32_t *extra_data = data;

	*extra_data = extra_value++;
}

static void extra_received(struct usfstl_rpc_connection *conn, const void *data)
{
	const uint32_t *extra_data = data;

	printf("extra %d\n", *extra_data);
}

int main(int argc, char **argv)
{
	g_usfstl_rpc_default_connection = USFSTL_RPC_LOCAL;

	USFSTL_RPC_LOCAL->extra_len = sizeof(uint32_t);
	USFSTL_RPC_LOCAL->extra_received = extra_received;
	USFSTL_RPC_LOCAL->extra_transmit = extra_transmit;

	// direct calls
	calls();
	recurse(3);

	return 0;
}
