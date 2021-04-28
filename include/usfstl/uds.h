/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * This defines a simple unix domain socket listen abstraction.
 */
#ifndef _USFSTL_UDS_H_
#define _USFSTL_UDS_H_
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include "list.h"

void usfstl_uds_create(const char *path, void (*connected)(int, void *),
		       void *data);
void usfstl_uds_remove(const char *path);

/**
 * usfstl_uds_connect_raw - make a raw unix domain socket connection
 * @path: the path to connect to
 *
 * This just returns the file descriptor for the connection, it
 * hasn't been added to any loop system yet etc. When the connection
 * is no longer needed, call close() on the returned fd.
 */
int usfstl_uds_connect_raw(const char *path);

int usfstl_uds_connect(const char *path, void (*readable)(int, void *),
		       void *data);
void usfstl_uds_disconnect(int fd);

#endif // _USFSTL_UDS_H_
