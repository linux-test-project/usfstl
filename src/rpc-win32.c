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

static inline void _usfstl_rpc_advance_buffers(size_t nbyte, WSABUF **buf_ptr, unsigned int *buf_cnt)
{
	while (nbyte) {
		size_t size = min(nbyte, (*buf_ptr)->len);

		nbyte -= size;
		(*buf_ptr)->len -= size;
		(*buf_ptr)->buf = ((char *)(*buf_ptr)->buf) + size;
		if ((*buf_ptr)->len == 0) {
			(*buf_ptr)++;
			(*buf_cnt)--;
		}
	}
}

void rpc_writev(usfstl_fd_t fd, unsigned int n, const struct write_vector *vectors)
{
	WSABUF bufs[n];
	unsigned int buf_cnt;
	DWORD bytes_sent, bufsize = 0;
	WSABUF *buf_ptr = bufs;

	for (buf_cnt = 0; buf_cnt < n; buf_cnt++) {
		bufs[buf_cnt].buf = (void *)vectors[buf_cnt].data;
		bufs[buf_cnt].len = vectors[buf_cnt].len;
		bufsize += vectors[buf_cnt].len;
	}

	while (bufsize) {
		assert(WSASend(fd, buf_ptr, buf_cnt, &bytes_sent, 0, NULL, NULL) == 0);
		assert(bytes_sent <= bufsize);
		bufsize -= bytes_sent;
		_usfstl_rpc_advance_buffers(bytes_sent, &buf_ptr, &buf_cnt);
	}
}

void rpc_readv(usfstl_fd_t fd, unsigned int n, const struct read_vector *vectors)
{
	WSABUF bufs[n];
	unsigned int buf_cnt;
	DWORD bytes_received, bufsize = 0;
	WSABUF *buf_ptr = bufs;
	DWORD flags = 0;

	for (buf_cnt = 0; buf_cnt < n; buf_cnt++) {
		bufs[buf_cnt].buf = vectors[buf_cnt].data;
		bufs[buf_cnt].len = vectors[buf_cnt].len;
		bufsize += vectors[buf_cnt].len;
	}

	while (bufsize) {
		assert(WSARecv(fd, buf_ptr, buf_cnt, &bytes_received, &flags, NULL, NULL) == 0);
		assert(bytes_received <= bufsize);
		bufsize -= bytes_received;
		_usfstl_rpc_advance_buffers(bytes_received, &buf_ptr, &buf_cnt);
	}
}
