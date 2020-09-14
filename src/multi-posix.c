/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <sys/socket.h>
#include <usfstl/multi.h>
#include "internal.h"

/* participant side */
#ifndef USFSTL_LIBRARY
static struct usfstl_rpc_connection USFSTL_NORESTORE_VAR(g_usfstl_multi_ctrl_conn_inst);

static bool usfstl_multi_init_control(struct usfstl_opt *opt, const char *arg)
{
	if (strncmp(arg, "fd:", 3) == 0) {
		unsigned long val;
		char *end;

		val = strtoul((char *)arg + 3, &end, 0);
		if (*end)
			return false;
		g_usfstl_multi_ctrl_conn_inst.conn.fd = val;
		g_usfstl_multi_ctrl_conn = &g_usfstl_multi_ctrl_conn_inst;

		return true;
	}

	return false;
}

USFSTL_OPT("control", 0, "connection", usfstl_multi_init_control, NULL,
	   "initialize external control");
#endif

/* controller side */
void usfstl_run_participant(struct usfstl_multi_participant *p, int nargs)
{
	int fds[2];
	pid_t pid;

	assert(socketpair(AF_LOCAL, SOCK_STREAM, 0, fds) == 0);
	p->conn->conn.fd = fds[0];

	if ((pid = fork()) == 0) {
		const char *_args[nargs + 4];
		char buf[100], namebuf[20 + strlen(p->name)];
		int i;

		_args[0] = p->binary;
		for (i = 0; i < nargs; i++)
			_args[i + 1] = p->args[i];

		close(fds[0]);
		sprintf(buf, "--control=fd:%d", fds[1]);
		_args[nargs + 1] = buf;
		sprintf(namebuf, "--multi-ptc-name=%s", p->name);
		_args[nargs + 2] = namebuf;
		_args[nargs + 3] = NULL;
		execv(p->binary, (char * const *)_args);
		assert(0);
	}

	p->pid = pid;
	close(fds[1]);
}
