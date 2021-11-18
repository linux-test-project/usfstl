/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include <usfstl/loop.h>
#include <usfstl/ctrluds.h>
#include <usfstl/uds.h>

#define USFSTL_MSG_SIZE		14400
#define USFSTL_MSG_HDR_SIZR (offsetof(struct usfstl_msg, msg))

struct usfstl_msg {
	uint32_t datalen;
	uint8_t msg[USFSTL_MSG_SIZE];
};

struct usfstl_ctrl_uds {
	struct usfstl_loop_entry loop_entry;
	struct usfstl_sched_ctrl *sched_ctrl;
	bool acked;
	void (*cb)(uint8_t *buf, uint32_t buf_len, int fd);
	void (*connect_cb)(void);
	void (*disconnect_cb)(void);
};

struct usfstl_msg_notif_entry {
	struct usfstl_job process_job;
	void (*cb)(uint8_t *buf, uint32_t buf_len, int fd);
	int fd;
	uint32_t datalen;
	uint8_t data[];
};

static void _usfsfl_msg_send(int socket_fd,
			     struct usfstl_sched_ctrl *sched_ctrl,
			     void *data, uint32_t datalen, int fd)
{
	struct usfstl_msg msg = {
		.datalen = datalen,
	};
	char control[CMSG_SPACE(sizeof(int))];
	struct iovec io = { .iov_base = &msg, .iov_len = sizeof(msg) };
	struct msghdr _msg = {
		.msg_iov = &io,
		.msg_iovlen = 1,
		.msg_control = control,
		.msg_controllen = sizeof(control),
	};
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&_msg);
	int ret;

	memcpy(msg.msg, data, datalen);

	memset(control, '\0', sizeof(control));

	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;

	if (fd < 0) {
		cmsg->cmsg_len = CMSG_LEN(0);
		ret = write(socket_fd, &msg, USFSTL_MSG_HDR_SIZR + datalen);
	} else {
		memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
		cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
		ret = sendmsg(socket_fd, &_msg, 0);
	}

	USFSTL_ASSERT(ret > 0, "usfstl_msg: send message failed err=%s",
		      strerror(errno));
}

static void _usfstl_process_notif(struct usfstl_job *job)
{
	struct usfstl_msg_notif_entry *notif =
		container_of(job, struct usfstl_msg_notif_entry, process_job);

	notif->cb(notif->data, notif->datalen, notif->fd);
	free(notif);
}

static void _usfstl_read_msg(struct usfstl_ctrl_uds *ctrl_uds)
{
	int fd = ctrl_uds->loop_entry.fd;
	int ret = 0;
	struct usfstl_msg msg;
	struct usfstl_msg_notif_entry *notif;
	struct cmsghdr *cmsg;
	char control[CMSG_SPACE(sizeof(int))] = { 0 };
	struct iovec io = { .iov_base = &msg, .iov_len = sizeof(msg) };
	struct msghdr _msg = {
		.msg_iov = &io,
		.msg_iovlen = 1,
		.msg_control = control,
		.msg_controllen = sizeof(control),
	};

	ret = recvmsg(fd, &_msg, 0);
	if (ret <= 0) {
		ctrl_uds->disconnect_cb();
		return;
	}

	if (!msg.datalen) {
		ctrl_uds->acked = true;
		return;
	}

	USFSTL_ASSERT(msg.datalen <= sizeof(msg.msg));

	notif = malloc(sizeof(*notif) + msg.datalen);
	USFSTL_ASSERT(notif);

	memset(&notif->process_job, 0, sizeof(notif->process_job));
	notif->datalen = msg.datalen;
	memcpy(notif->data, msg.msg, msg.datalen);
	notif->cb = ctrl_uds->cb;

	cmsg = CMSG_FIRSTHDR(&_msg);
	if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
	    cmsg->cmsg_len == CMSG_LEN(sizeof(notif->fd)))
		memcpy(&notif->fd, CMSG_DATA(cmsg), sizeof(notif->fd));
	else
		notif->fd = -1;

	usfstl_sched_ctrl_sync_from(ctrl_uds->sched_ctrl);

	notif->process_job.priority = 0x7fffffff;
	notif->process_job.name = "notif-job";
	notif->process_job.callback = _usfstl_process_notif;

	notif->process_job.start = usfstl_sched_current_time(ctrl_uds->sched_ctrl->sched);
	usfstl_sched_add_job(ctrl_uds->sched_ctrl->sched, &notif->process_job);

	_usfsfl_msg_send(fd, ctrl_uds->sched_ctrl, NULL, 0, -1);
}

void usfstl_msg_send(struct usfstl_ctrl_uds *ctrl_uds, uint8_t *data,
		     uint32_t datalen, int fd)
{
	// Sending the message may trigger the recipient to ask for runtime, so update the
	// time controller with the current internal time.
	usfstl_sched_ctrl_sync_to(ctrl_uds->sched_ctrl);

	USFSTL_ASSERT(ctrl_uds->acked == false);
	_usfsfl_msg_send(ctrl_uds->loop_entry.fd, ctrl_uds->sched_ctrl, data,
			 datalen, fd);

	do {
		usfstl_loop_wait_and_handle();
	} while (!ctrl_uds->acked);

	ctrl_uds->acked = false;
}

static void loop_handle_message(struct usfstl_loop_entry *entry)
{
	struct usfstl_ctrl_uds *ctrl_uds =
		container_of(entry, struct usfstl_ctrl_uds, loop_entry);

	USFSTL_ASSERT(ctrl_uds->cb);

	_usfstl_read_msg(ctrl_uds);
}

static void _usfstl_handle_connection(int fd, void *data)
{
	struct usfstl_ctrl_uds *ctrl_uds = data;

	ctrl_uds->loop_entry.fd = fd;

	usfstl_loop_register(&ctrl_uds->loop_entry);

	if (ctrl_uds->connect_cb)
		ctrl_uds->connect_cb();
}

struct usfstl_ctrl_uds *
usfstl_ctrl_uds_server_init(char *name, struct usfstl_sched_ctrl *ctrl,
			    void (*msg_cb)(uint8_t *buf, uint32_t buf_len, int fd),
			    void (*connect_cb)(void),
			    void (*disconnect_cb)(void))
{
	struct usfstl_ctrl_uds *ctrl_uds;

	USFSTL_ASSERT(ctrl);

	ctrl_uds = malloc(sizeof(*ctrl_uds));
	USFSTL_ASSERT(ctrl_uds);

	ctrl_uds->loop_entry.handler = loop_handle_message;
	ctrl_uds->sched_ctrl = ctrl;
	ctrl_uds->acked = false;
	ctrl_uds->cb = msg_cb;
	ctrl_uds->connect_cb = connect_cb;
	ctrl_uds->disconnect_cb = disconnect_cb;

	usfstl_uds_create(name, _usfstl_handle_connection, ctrl_uds);

	return ctrl_uds;
}

struct usfstl_ctrl_uds *
usfstl_ctrl_uds_client_init(char *name, struct usfstl_sched_ctrl *ctrl,
			    void (*cb)(uint8_t *buf, uint32_t buf_len, int fd),
			    void (*disconnect_cb)(void))
{
	int fd;
	struct usfstl_ctrl_uds *ctrl_uds;

	USFSTL_ASSERT(ctrl);

	fd = usfstl_uds_connect_raw(name);

	ctrl_uds = malloc(sizeof(*ctrl_uds));
	USFSTL_ASSERT(ctrl_uds);

	ctrl_uds->loop_entry.fd = fd;
	ctrl_uds->loop_entry.handler = loop_handle_message;
	ctrl_uds->sched_ctrl = ctrl;
	ctrl_uds->acked = false;
	ctrl_uds->cb = cb;
	ctrl_uds->disconnect_cb = disconnect_cb;
	usfstl_loop_register(&ctrl_uds->loop_entry);

	return ctrl_uds;
}

void usfstl_ctrl_uds_deinit(struct usfstl_ctrl_uds *ctrl_uds)
{
	usfstl_loop_unregister(&ctrl_uds->loop_entry);
	close(ctrl_uds->loop_entry.fd);
	free(ctrl_uds);
}
