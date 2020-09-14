/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <unistd.h>
#include <assert.h>
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <windows.h>
#include "internal.h"

void rpc_write(usfstl_fd_t fd, const void *buf, size_t bufsize)
{
	const char *cbuf = buf;
	unsigned int offs = 0;
	int ret;

	while (bufsize) {
		ret = send(fd, cbuf + offs, bufsize, 0);
		assert(ret > 0 && (size_t)ret <= bufsize);
		offs += ret;
		bufsize -= ret;
	}
}

void rpc_read(usfstl_fd_t fd, void *buf, size_t nbyte)
{
	char *cbuf = buf;
	unsigned int offs = 0;
	int ret;

	while (nbyte) {
		ret = recv(fd, cbuf + offs, nbyte, 0);
		assert(ret > 0 && (size_t)ret <= nbyte);
		offs += ret;
		nbyte -= ret;
	}
}
