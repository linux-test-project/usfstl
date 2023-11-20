/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/param.h>
#include "internal.h"

void rpc_write(int fd, const void *buf, size_t bufsize)
{
	const char *cbuf = buf;
	unsigned int offs = 0;
	ssize_t ret;

	while (bufsize) {
		ret = write(fd, cbuf + offs, bufsize);
		if (ret < 0 && errno == EINTR)
			continue;

		assert(ret > 0 && (size_t)ret <= bufsize);
		offs += ret;
		bufsize -= ret;
	}
}

void rpc_read(int fd, void *buf, size_t nbyte)
{
	char *cbuf = buf;
	unsigned int offs = 0;
	ssize_t ret;

	while (nbyte) {
		ret = read(fd, cbuf + offs, nbyte);
		if (ret < 0 && errno == EINTR)
			continue;

		assert(ret > 0 && (size_t)ret <= nbyte);
		offs += ret;
		nbyte -= ret;
	}
}

static inline void _usfstl_rpc_advance_buffers(size_t nbyte, struct iovec **buf_ptr, unsigned int *buf_cnt)
{
	while (nbyte) {
		size_t size = MIN(nbyte, (*buf_ptr)->iov_len);

		nbyte -= size;
		(*buf_ptr)->iov_len -= size;
		(*buf_ptr)->iov_base = ((char *)(*buf_ptr)->iov_base) + size;
		if ((*buf_ptr)->iov_len == 0) {
			(*buf_ptr)++;
			(*buf_cnt)--;
		}
	}
}

void rpc_writev(int fd, unsigned int n, const struct write_vector *vectors)
{
	struct iovec iov[n];
	ssize_t ret;
	unsigned int buf_cnt;
	struct iovec *buf_ptr = iov;
	size_t bufsize = 0;

	for (buf_cnt = 0; buf_cnt < n; buf_cnt++) {
		iov[buf_cnt].iov_base = (void *)vectors[buf_cnt].data;
		iov[buf_cnt].iov_len = vectors[buf_cnt].len;
		bufsize += vectors[buf_cnt].len;
	}

	while (bufsize) {
		ret = writev(fd, buf_ptr, buf_cnt);
		if (ret < 0 && errno == EINTR)
			continue;

		assert(ret > 0 && (size_t)ret <= bufsize);
		bufsize -= ret;
		_usfstl_rpc_advance_buffers(ret, &buf_ptr, &buf_cnt);
	}
}

void rpc_readv(int fd, unsigned int n, const struct read_vector *vectors)
{
	struct iovec iov[n];
	ssize_t ret;
	unsigned int buf_cnt;
	struct iovec *buf_ptr = iov;
	size_t bufsize = 0;

	for (buf_cnt = 0; buf_cnt < n; buf_cnt++) {
		iov[buf_cnt].iov_base = vectors[buf_cnt].data;
		iov[buf_cnt].iov_len = vectors[buf_cnt].len;
		bufsize += vectors[buf_cnt].len;
	}

	while (bufsize) {
		ret = readv(fd, buf_ptr, buf_cnt);
		if (ret < 0 && errno == EINTR)
			continue;

		assert(ret > 0 && (size_t)ret <= bufsize);
		bufsize -= ret;
		_usfstl_rpc_advance_buffers(ret, &buf_ptr, &buf_cnt);
	}
}
