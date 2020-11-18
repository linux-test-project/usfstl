/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <usfstl/log.h>
#include <usfstl/multi.h>
#include "print-rpc.h"
#include "internal.h"

struct usfstl_logger {
	const char *name;
	FILE *f;
	uint32_t refcount;
	int idx, remote_idx;
};

static struct usfstl_logger **USFSTL_NORESTORE_VAR(g_usfstl_loggers);
static int USFSTL_NORESTORE_VAR(g_usfstl_loggers_num);

static int usfstl_find_or_allocate(const char *name)
{
	int i;

	for (i = 0; i < g_usfstl_loggers_num; i++) {
		if (!g_usfstl_loggers[i])
			continue;
		if (!strcmp(g_usfstl_loggers[i]->name, name))
			goto count;
	}

	for (i = 0; i < g_usfstl_loggers_num; i++) {
		if (!g_usfstl_loggers[i])
			goto set;
	}

	i = g_usfstl_loggers_num;
	g_usfstl_loggers_num++;

	g_usfstl_loggers = realloc(g_usfstl_loggers,
				 sizeof(g_usfstl_loggers[0]) *
					g_usfstl_loggers_num);
	USFSTL_ASSERT(g_usfstl_loggers);

set:
	g_usfstl_loggers[i] = calloc(1, sizeof(struct usfstl_logger));
	USFSTL_ASSERT(g_usfstl_loggers[i]);
	g_usfstl_loggers[i]->name = strdup(name);
	USFSTL_ASSERT(g_usfstl_loggers[i]->name);
	g_usfstl_loggers[i]->idx = i;
count:
	g_usfstl_loggers[i]->refcount++;
	return i;
}

void usfstl_vprintf(const char *msg, va_list ap)
{
	vprintf(msg, ap);
}

void usfstl_printf(const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	usfstl_vprintf(msg, ap);
	va_end(ap);

	if (g_usfstl_flush_each_log)
		fflush(stdout);
}

static int32_t usfstl_log_create_logfile(const char *name)
{
	if (usfstl_is_multi_participant()) {
		struct {
			struct usfstl_rpc_log_create hdr;
			char name[1000];
		} __attribute__((packed)) msg;

		memcpy(msg.name, name, sizeof(msg.name));
		return rpc_log_create_conn(g_usfstl_multi_ctrl_conn, &msg.hdr,
					   sizeof(msg.hdr) + strlen(msg.name));
	} else {
		/* just overwrite a possibly existing file */
		FILE *f = fopen(name, "w");
		USFSTL_ASSERT(f, "failed to create '%s'", name);
		fclose(f);
		return -1;
	}
}

struct usfstl_logger *usfstl_log_create(const char *name)
{
	int idx = usfstl_find_or_allocate(name);
	struct usfstl_logger *logger = g_usfstl_loggers[idx];

	if (!logger->f) {
		logger->remote_idx = usfstl_log_create_logfile(name);
		logger->f = fopen(name, "a");
		USFSTL_ASSERT(logger->f, "failed to open '%s'", name);
	}

	return logger;
}

static void usfstl_log_close(struct usfstl_logger *logger)
{
	if (logger->f == stdout)
		return;

	fclose(logger->f);

	if (usfstl_is_multi_participant())
		rpc_log_close_conn(g_usfstl_multi_ctrl_conn,
				   logger->remote_idx);
}

struct usfstl_logger *usfstl_log_create_stdout(const char *name)
{
	uint32_t idx = usfstl_find_or_allocate(name);
	struct usfstl_logger *logger = g_usfstl_loggers[idx];

	// may change an existing logger to be stdout
	if (logger->f)
		usfstl_log_close(logger);
	logger->f = stdout;

	return logger;
}

void usfstl_log_free(struct usfstl_logger *logger)
{
	int i;
	bool empty = true;

	logger->refcount--;

	if (logger->refcount)
		return;

	usfstl_log_close(logger);

	free((void *)logger->name);
	g_usfstl_loggers[logger->idx] = NULL;
	free(logger);

	for (i = 0; i < g_usfstl_loggers_num; i++) {
		if (g_usfstl_loggers[i]) {
			empty = false;
			break;
		}
	}

	if (empty) {
		free(g_usfstl_loggers);
		g_usfstl_loggers = NULL;
		g_usfstl_loggers_num = 0;
	}
}

static void _usfstl_logvf(struct usfstl_logger *logger, const char *msg, va_list ap)
{
	if (!logger)
		return;

	vfprintf(logger->f, msg, ap);
}

static void _usfstl_logf(struct usfstl_logger *logger, const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	_usfstl_logvf(logger, msg, ap);
	va_end(ap);
}

void usfstl_logf(struct usfstl_logger *logger, const char *pfx, const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	usfstl_logvf(logger, pfx, msg, ap);
	va_end(ap);
}

static void usfstl_log_flush(struct usfstl_logger *logger)
{
	if (!logger)
		return;

	fflush(logger->f);
}

void usfstl_flush_all(void)
{
	int i;

	fflush(stdout);
	fflush(stderr);

	for (i = 0; i < g_usfstl_loggers_num; i++) {
		if (!g_usfstl_loggers[i])
			continue;
		usfstl_log_flush(g_usfstl_loggers[i]);
	}
}

void usfstl_logvf(struct usfstl_logger *logger, const char *pfx,
		  const char *msg, va_list ap)
{
	if (!logger)
		return;

	if (g_usfstl_multi_local_participant.name &&
	    (usfstl_is_multi_controller() || usfstl_is_multi_participant()))
		_usfstl_logf(logger, "[%s]", g_usfstl_multi_local_participant.name);

	if (pfx && pfx[0])
		_usfstl_logf(logger, "%s", pfx);
	_usfstl_logvf(logger, msg, ap);

	if (msg[strlen(msg)-1] != '\n')
		_usfstl_logf(logger, "\n");

	usfstl_log_flush(logger);
}

void usfstl_logf_buf(struct usfstl_logger *logger, const char *pfx,
		     const void *buf, size_t buf_len,
		     unsigned int buf_item_size, const char *buf_item_format,
		     const char *format, ...)
{
	unsigned int i;
	va_list ap;

	if (!logger)
		return;

	if (pfx && pfx[0])
		_usfstl_logf(logger, "%s", pfx);
	va_start(ap, format);
	_usfstl_logvf(logger, format, ap);
	va_end(ap);

	switch (buf_item_size) {
	case 1:
		for (i = 0; i < buf_len; i++)
			_usfstl_logf(logger, buf_item_format, ((unsigned char *)buf)[i]);
		break;
	case 2:
		for (i = 0; i < buf_len; i++)
			_usfstl_logf(logger, buf_item_format, ((unsigned short *)buf)[i]);
		break;
	case 4:
		for (i = 0; i < buf_len; i++)
			_usfstl_logf(logger, buf_item_format, ((unsigned int *)buf)[i]);
		break;
	default:
		assert(0);
	}

	if (buf_item_format[strlen(buf_item_format) - 1] != '\n')
		_usfstl_logf(logger, "\n");

	usfstl_log_flush(logger);
}

#define USFSTL_RPC_CALLEE_STUB
#include "print-rpc.h"
#undef USFSTL_RPC_CALLEE_STUB

#define USFSTL_RPC_CALLER_STUB
#include "print-rpc.h"
#undef USFSTL_RPC_CALLER_STUB

#define USFSTL_RPC_IMPLEMENTATION
#include <usfstl/rpc.h>

USFSTL_RPC_METHOD_VAR(int, rpc_log_create, struct usfstl_rpc_log_create)
{
	unsigned int namelen = insize - sizeof(*in);
	char name[namelen + 1];
	struct usfstl_logger *logger;

	sprintf(name, "%.*s", namelen, in->name);
	name[namelen] = 0;

	logger = usfstl_log_create(name);

	return logger->idx;
}

USFSTL_RPC_VOID_METHOD(rpc_log_close, int)
{
	USFSTL_ASSERT(in < g_usfstl_loggers_num);

	usfstl_log_free(g_usfstl_loggers[in]);
}
