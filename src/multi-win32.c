/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <usfstl/multi.h>
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <windows.h>
#include "internal.h"

/* participant side */
#ifndef USFSTL_LIBRARY
static struct usfstl_rpc_connection USFSTL_NORESTORE_VAR(g_usfstl_multi_ctrl_conn_inst);

static bool usfstl_multi_init_control(struct usfstl_opt *opt, const char *arg)
{
	if (strncmp(arg, "tcp:", 4) == 0) {
		struct sockaddr_in addr = {
			.sin_family = AF_INET,
			.sin_addr.S_un.S_un_b = { 127, 0, 0, 1 },
		};
		unsigned long val;
		char *end;
		SOCKET s;
		WSADATA wsa;

		assert(WSAStartup(MAKEWORD(2, 0), &wsa) == 0);
		assert(wsa.wVersion == MAKEWORD(2, 0));

		val = strtoul((char *)arg + 4, &end, 0);
		if (*end)
			return false;

		addr.sin_port = htons(val);

		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		assert(s != INVALID_SOCKET);
		assert(connect(s, (void *)&addr, sizeof(addr)) == 0);

		g_usfstl_multi_ctrl_conn_inst.conn.fd = s;
		g_usfstl_multi_ctrl_conn = &g_usfstl_multi_ctrl_conn_inst;

		return true;
	}

	return false;
}

USFSTL_OPT("control", 0, "connection", usfstl_multi_init_control, NULL,
	   "initialize external control");
#endif

/* controller side */
static SOCKET USFSTL_NORESTORE_VAR(g_usfstl_multi_server_socket) = INVALID_SOCKET;
static uint64_t USFSTL_NORESTORE_VAR(g_usfstl_multi_server_port);

void usfstl_run_participant(struct usfstl_multi_participant *p, int nargs)
{
	STARTUPINFO si = { .cb = sizeof(si) };
	PROCESS_INFORMATION pi = {};
	char cmdline[1000] = {};
	char tcpopt[100] = {};
	char namebuf[20 + strlen(p->name)];
	SOCKET s;
	int i;

	if (g_usfstl_multi_server_socket == INVALID_SOCKET) {
		struct sockaddr_in addr = {
			.sin_family = AF_INET,
			.sin_addr.S_un.S_un_b = { 127, 0, 0, 1 },
			.sin_port = 0,
		};
		int addrsize = sizeof(addr);
		WSADATA wsa;

		assert(WSAStartup(MAKEWORD(2, 0), &wsa) == 0);
		assert(wsa.wVersion == MAKEWORD(2, 0));

		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		assert(s != INVALID_SOCKET);
		assert(bind(s, (void *)&addr, sizeof(addr)) == 0);
		assert(getsockname(s, (void *)&addr, &addrsize) == 0);
		assert(addrsize == sizeof(addr));
		assert(listen(s, SOMAXCONN) == 0);
		g_usfstl_multi_server_port = ntohs(addr.sin_port);
		g_usfstl_multi_server_socket = s;
	}

	strcat(cmdline, p->binary);
	for (i = 0; i < nargs; i++) {
		assert(strlen(cmdline) + strlen(p->args[i]) + 2 < sizeof(cmdline));
		strcat(cmdline, " ");
		strcat(cmdline, p->args[i]);
	}

	sprintf(tcpopt, "--control=tcp:%d", (unsigned int)g_usfstl_multi_server_port);
	sprintf(namebuf, "--multi-ptc-name=%s", p->name);
	assert(strlen(cmdline) + strlen(tcpopt) + strlen(namebuf) + 3 < sizeof(cmdline));
	strcat(cmdline, " ");
	strcat(cmdline, tcpopt);
	strcat(cmdline, " ");
	strcat(cmdline, namebuf);

	assert(CreateProcess(NULL,
			     cmdline,
			     NULL,	// lpProcessAttributes
			     NULL,	// lpThreadAttributes
			     FALSE,	// bInheritHandles
			     0,		// dwCreationFlags
			     NULL,	// lpEnvironment
			     NULL,	// lpCurrentDirectory
			     &si, &pi));

	s = accept(g_usfstl_multi_server_socket, NULL, 0);
	assert(s != INVALID_SOCKET);

	p->pid = pi.dwProcessId;
	p->conn->conn.fd = s;
}
