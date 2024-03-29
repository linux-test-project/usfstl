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

/**
 * usfstl_uds_create - create a new unix domain socket server
 * @path: the path to create the server at
 * @connected: callback to be called when a new client connects,
 *	called with the fd and the data
 * @data: the data to pass to the @connected callback
 */
void usfstl_uds_create(const char *path, void (*connected)(int, void *),
		       void *data);

/**
 * usfstl_uds_remove - remove a unix domain socket server
 * @path: the path indicating which server to remove
 */
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

/**
 * usfstl_uds_connect - make a unix domain socket connection
 * @path: the path to connect to
 * @readable: the callback when the fd becomes readable, called
 *	with the fd and the data
 * @data: the data to pass to the @readable callback
 *
 * This also adds the new connection to the usfstl loop and
 * calls the readable handler (with the fd and data) when the
 * fd becomes readable.
 */
int usfstl_uds_connect(const char *path, void (*readable)(int, void *),
		       void *data);

/**
 * usfstl_uds_disconnect - disconnect a unix domain socket connection
 * @fd: the fd for the connection
 */
void usfstl_uds_disconnect(int fd);

#endif // _USFSTL_UDS_H_
