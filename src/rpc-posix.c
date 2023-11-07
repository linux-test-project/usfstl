/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <unistd.h>
#include <assert.h>
#include <errno.h>
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
