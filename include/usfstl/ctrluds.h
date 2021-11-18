/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _USFSTL_MSG_H
#define _USFSTL_MSG_H

#include <usfstl/schedctrl.h>

struct usfstl_ctrl_uds;

/**
 * usfstl_ctrl_uds_client_init - create a messaging client
 * @path: the path to connect to
 * @ctrl: scheduler control
 * @cb: a callback for handling incoming messages, called with the incoming
 *	message and an optional file descriptor sent as message metadata
 *	(zero if not received)
 * @disconnect_cb: callback to be called when the client disconnects
 *
 * Returns a handle to the newly created client. This handle should be used in
 * calls to usfstl_msg_send()
 */
struct usfstl_ctrl_uds *
usfstl_ctrl_uds_client_init(char *path, struct usfstl_sched_ctrl *ctrl,
			    void (*cb)(uint8_t *buf, uint32_t buf_len, int fd),
			    void (*disconnect_cb)(void));

/**
 * usfstl_ctrl_uds_server_init - create a messaging server
 * @path: the path to create the server at
 * @ctrl: scheduler control
 * @msg_cb: a callback for handling incoming messages, called with the incoming
 *	message and an optional file descriptor sent as message metadata
 *	(zero if not received)
 * @connect_cb: callback to be called when a new client connects
 * @disconnect_cb: callback to be called when a client disconnects
 *
 * Returns a handle to the newly created server. This handle should be used in
 * calls to usfstl_msg_send()
 */
struct usfstl_ctrl_uds *
usfstl_ctrl_uds_server_init(char *path, struct usfstl_sched_ctrl *ctrl,
			    void (*msg_cb)(uint8_t *buf, uint32_t buf_len, int fd),
			    void (*connect_cb)(void),
			    void (*disconnect_cb)(void));

/**
 * usfstl_ctrl_uds_deinit - remove a previously added client/server
 * @ctrl_uds: a handle to the client/server to deinit
 */
void usfstl_ctrl_uds_deinit(struct usfstl_ctrl_uds *ctrl_uds);

/**
 * usfstl_msg_send - send a message
 * @ctrl_uds: a handle to the client/server that send the message
 * @data: a pointer to the data to send
 * @datalen: the length of @data in bytes
 * @fd: a file descriptor to send. Optional, negative value if not applicable.
 */
void usfstl_msg_send(struct usfstl_ctrl_uds *ctrl_uds, uint8_t *data,
		     uint32_t datalen, int fd);

#endif
