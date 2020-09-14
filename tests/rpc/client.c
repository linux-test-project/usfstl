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
#include <sys/socket.h>
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

	printf("client received extra %d\n", *extra_data);
}

int main(int argc, char **argv)
{
	struct usfstl_rpc_connection conn = {
		.extra_len = sizeof(uint32_t),
		.extra_received = extra_received,
		.extra_transmit = extra_transmit,
	};
	pid_t pid;
	int fds[2];
	int status = 0;

	// direct call
	printf("%d\n", callme1_conn(USFSTL_RPC_LOCAL, 100));

	assert(socketpair(AF_LOCAL, SOCK_STREAM, 0, fds) == 0);

	if ((pid = fork()) == 0) {
		close(fds[0]);
		if (fds[1] != 4) {
			dup2(fds[1], 4);
			close(fds[1]);
		}
		execl("./server", "./server", NULL);
	}
	close(fds[1]);

	conn.conn.fd = fds[0];
	g_usfstl_rpc_default_connection = &conn;

	// and this needs the server connection
	calls();

	recurse(3);

	quit_conn(&conn, 17);
	assert(waitpid(pid, &status, 0) == pid);
	assert(WIFEXITED(status));
	assert(WEXITSTATUS(status) == 17);

	return 0;
}
