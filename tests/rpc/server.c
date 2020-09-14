/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <usfstl/rpc.h>

extern int exitstatus;

static uint32_t extra_value = 10000000;

static void extra_transmit(struct usfstl_rpc_connection *conn, void *data)
{
	uint32_t *extra_data = data;

	*extra_data = extra_value++;
}

static void extra_received(struct usfstl_rpc_connection *conn, const void *data)
{
	const uint32_t *extra_data = data;

	printf("server received extra %d\n", *extra_data);
}

int main(int argc, char **argv)
{
	struct usfstl_rpc_connection conn = {
		.conn.fd = 4,
		.extra_len = sizeof(uint32_t),
		.extra_received = extra_received,
		.extra_transmit = extra_transmit,
	};

	g_usfstl_rpc_default_connection = &conn;
	usfstl_rpc_add_connection(&conn);

	while (!exitstatus)
		usfstl_rpc_handle();
	fprintf(stderr, "server exiting with status %d\n", exitstatus);
	usfstl_rpc_del_connection(&conn);
	return exitstatus;
}
