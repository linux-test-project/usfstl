/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _USFSTL_LOG_H_
#define _USFSTL_LOG_H_
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include "assert.h"
#include "opt.h"

/*
 * usfstl_printf - print a simple message to stdout
 */
void __attribute__((format(printf, 1, 2)))
usfstl_printf(const char *msg, ...);

struct usfstl_logger;

/**
 * usfstl_log_create - create logger
 * @name: filename for the logger
 */
struct usfstl_logger *usfstl_log_create(const char *name);

/**
 * usfstl_log_create_stdout - create logger to stdout
 * @name: the filename that would normally be used
 */
struct usfstl_logger *usfstl_log_create_stdout(const char *name);

/**
 * usfstl_log_free - free logger
 * @f: the logger
 */
void usfstl_log_free(struct usfstl_logger *logger);

/**
 * usfstl_log_set_tagging - enable or disable logger tagging
 * @logger: the logger
 * @enable: whether or not each log line should be prefixed with
 *	participant name tag (enabled by default)
 */
void usfstl_log_set_tagging(struct usfstl_logger *logger, bool enable);

/*
 * usfstl_logf - logging to per-test file
 */
void __attribute__((format(printf, 3, 4)))
usfstl_logf(struct usfstl_logger *logger, const char *pfx,
	    const char *msg, ...);

/*
 * usfstl_logvf - logging to file, with va_list
 */
void usfstl_logvf(struct usfstl_logger *logger, const char *pfx,
		  const char *msg, va_list ap);

/*
 * usfstl_logf_buf - log including buffer items
 */
void __attribute__((format(printf, 7, 8)))
usfstl_logf_buf(struct usfstl_logger *logger, const char *pfx,
		const void *buf, size_t buf_len,
		unsigned int buf_item_size, const char *buf_item_format,
		const char *format, ...);

#endif // _USFSTL_LOG_H_
