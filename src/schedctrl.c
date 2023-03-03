/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sys/mman.h>
#include <sys/socket.h>
#include <usfstl/uds.h>
#include <usfstl/schedctrl.h>
#include "internal.h"
#include <stdlib.h>

#define DEBUG_LEVEL 0

#define _DBG(lvl, fmt, ...)	do {					\
	 if (lvl <= DEBUG_LEVEL) {					\
		fprintf(ctrl->shm.flog,					\
			"[%*d][%*" PRIu64 "][*id:%" PRIx64 "]" fmt "\n",\
			2, (int)ctrl->shm.id,				\
			12, (uint64_t)ctrl->shm.mem->current_time,	\
			(uint64_t)ctrl->shm.mem->clients[ctrl->shm.id].name,\
			##__VA_ARGS__);					\
		fflush(ctrl->shm.flog);					\
	}								\
} while (0)
#define DBG_SHAREDMEM(lvl, fmt, ...) _DBG(lvl, " " fmt, ##__VA_ARGS__)

static void _usfstl_sched_ctrl_send_msg(struct usfstl_sched_ctrl *ctrl,
					enum um_timetravel_ops op,
					uint64_t time, uint32_t seq)
{
	struct um_timetravel_msg msg = {
		.op = op,
		.seq = seq,
		.time = time,
	};

	USFSTL_ASSERT_EQ((int)write(ctrl->fd, &msg, sizeof(msg)),
			 (int)sizeof(msg), "%d");
}

static int _sched_ctrl_get_msg_fds(struct msghdr *msghdr,
				   int *outfds, int max_fds)
{
	struct cmsghdr *msg;
	int fds;

	for (msg = CMSG_FIRSTHDR(msghdr); msg; msg = CMSG_NXTHDR(msghdr, msg)) {
		if (msg->cmsg_level != SOL_SOCKET || msg->cmsg_type != SCM_RIGHTS)
			continue;

		fds = (msg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
		USFSTL_ASSERT(fds <= max_fds);
		memcpy(outfds, CMSG_DATA(msg), fds * sizeof(int));
		return fds;
	}

	return 0;
}

static void _sched_ctrl_handle_fds(struct usfstl_sched_ctrl *ctrl,
				   struct msghdr *msghdr)
{
	int msg_fds[UM_TIMETRAVEL_MAX_FDS];
	struct um_timetravel_msg *msg = msghdr->msg_iov->iov_base;
	int num_fds = _sched_ctrl_get_msg_fds(msghdr, msg_fds, UM_TIMETRAVEL_MAX_FDS);

	if (!num_fds)
		return;

	if (ctrl->handle_msg_fds)
		ctrl->handle_msg_fds(ctrl, msg, msg_fds, num_fds);

	/* Always close all FDs, if any callback needs the FD it should dup it */
	for (int i = 0; i < num_fds; i++)
		close(msg_fds[i]);
}

static void usfstl_sched_ctrl_sock_read(int fd, void *data)
{
	struct usfstl_sched_ctrl *ctrl = data;
	struct um_timetravel_msg msg;
	int8_t msg_control[CMSG_SPACE(sizeof(int) * UM_TIMETRAVEL_MAX_FDS)];
	struct iovec iov = {
		.iov_base = &msg,
		.iov_len = sizeof(msg),
	};
	struct msghdr msghdr = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = msg_control,
		.msg_controllen = sizeof(msg_control),
	};
	int sz = recvmsg(fd, &msghdr, 0);
	uint64_t time;

	USFSTL_ASSERT_EQ(sz, (int)sizeof(msg), "%d");

	switch (msg.op) {
	case UM_TIMETRAVEL_ACK:
		if (msg.seq == ctrl->expected_ack_seq) {
			ctrl->acked = 1;
			ctrl->ack_time = msg.time;
			_sched_ctrl_handle_fds(ctrl, &msghdr);
		}
		return;
	case UM_TIMETRAVEL_RUN:
		ctrl->waiting = 0;

		/* No ack or set time is needed in shared mem run */
		if (ctrl->shm.mem) {
			/* assert internal state is as external state */
			USFSTL_ASSERT_EQ(ctrl->shm.mem->running_id,
					 ctrl->shm.id, "%d");
			return;
		}

		time = DIV_ROUND_UP(msg.time - ctrl->offset,
				    ctrl->nsec_per_tick);
		usfstl_sched_set_time(ctrl->sched, time);
		break;
	case UM_TIMETRAVEL_FREE_UNTIL:
		/* round down here, so we don't overshoot */
		time = (msg.time - ctrl->offset) / ctrl->nsec_per_tick;
		usfstl_sched_set_sync_time(ctrl->sched, time);
		break;
	case UM_TIMETRAVEL_BROADCAST:
		if (ctrl->handle_bc_message)
			ctrl->handle_bc_message(ctrl, msg.time);
		break;
	case UM_TIMETRAVEL_START:
	case UM_TIMETRAVEL_REQUEST:
	case UM_TIMETRAVEL_WAIT:
	case UM_TIMETRAVEL_GET:
	case UM_TIMETRAVEL_UPDATE:
	case UM_TIMETRAVEL_GET_TOD:
		USFSTL_ASSERT(0);
		return;
	}

	_usfstl_sched_ctrl_send_msg(ctrl, UM_TIMETRAVEL_ACK, 0, msg.seq);
}

static void usfstl_sched_ctrl_send_msg(struct usfstl_sched_ctrl *ctrl,
				       enum um_timetravel_ops op,
				       uint64_t time)
{
	static uint32_t seq, old_expected;

	do {
		seq++;
	} while (seq == 0);

	_usfstl_sched_ctrl_send_msg(ctrl, op, time, seq);

	/* No Ack is expected in shared memory mode */
	if (ctrl->shm.mem && op == UM_TIMETRAVEL_WAIT)
		return;

	old_expected = ctrl->expected_ack_seq;
	ctrl->expected_ack_seq = seq;

	USFSTL_ASSERT_EQ((int)ctrl->acked, 0, "%d");

	/*
	 * Race alert!
	 *
	 * UM_TIMETRAVEL_WAIT basically passes the run "token" to the
	 * controller, which passes it to another participant of the
	 * simulation. This other participant might immediately send
	 * us another message on a different channel, e.g. if this
	 * code is used in a vhost-user device.
	 *
	 * If here we were to use use usfstl_loop_wait_and_handle(),
	 * we could actually get and process the vhost-user message
	 * before the ACK for the WAIT message here, depending on the
	 * (host) kernel's message ordering and select() handling etc.
	 *
	 * To avoid this, directly read the ACK message for the WAIT,
	 * without handling any other sockets (first).
	 */
	if (op == UM_TIMETRAVEL_WAIT) {
		usfstl_sched_ctrl_sock_read(ctrl->fd, ctrl);
		USFSTL_ASSERT(ctrl->acked);
	}

	while (!ctrl->acked)
		usfstl_loop_wait_and_handle();
	ctrl->acked = 0;
	ctrl->expected_ack_seq = old_expected;

	if (op == UM_TIMETRAVEL_GET) {
		if (ctrl->frozen) {
			uint64_t local;

			local = usfstl_sched_current_time(ctrl->sched) * ctrl->nsec_per_tick;
			ctrl->offset = ctrl->ack_time - local;
		} else {
			uint64_t time;

			time = DIV_ROUND_UP(ctrl->ack_time - ctrl->offset,
					    ctrl->nsec_per_tick);
			usfstl_sched_set_time(ctrl->sched, time);
		}
	}
}

void usfstl_sched_ctrl_send_bc(struct usfstl_sched_ctrl *ctrl, uint64_t bc_message)
{
	USFSTL_ASSERT(ctrl->started, "Cannot send braodcast message until started");

	usfstl_sched_ctrl_send_msg(ctrl, UM_TIMETRAVEL_BROADCAST, bc_message);
}

static enum usfstl_sched_req_status
usfstl_sched_ctrl_request(struct usfstl_scheduler *sched, uint64_t time)
{
	struct usfstl_sched_ctrl *ctrl = sched->ext.ctrl;

	if (!ctrl->started)
		return USFSTL_SCHED_REQ_STATUS_CAN_RUN;

	usfstl_sched_ctrl_send_msg(ctrl, UM_TIMETRAVEL_REQUEST,
				   time * ctrl->nsec_per_tick + ctrl->offset);
	return USFSTL_SCHED_REQ_STATUS_WAIT;
}

static void usfstl_sched_ctrl_wait(struct usfstl_scheduler *sched)
{
	struct usfstl_sched_ctrl *ctrl = sched->ext.ctrl;

	ctrl->waiting = 1;
	usfstl_sched_ctrl_send_msg(ctrl, UM_TIMETRAVEL_WAIT, -1);

	while (ctrl->waiting)
		usfstl_loop_wait_and_handle();

	if (ctrl->shm.mem && ctrl->started) {
		/* Assert internal state is as external state */
		USFSTL_ASSERT_EQ(ctrl->shm.mem->running_id,
				 ctrl->shm.id, "%d");
		/* Clear any past request we had */
		ctrl->shm.mem->clients[ctrl->shm.id].flags &=
			~UM_TIMETRAVEL_SCHEDSHM_FLAGS_REQ_RUN;
	}
}

void usfstl_sched_ctrl_yield(struct usfstl_sched_ctrl *ctrl)
{
	uint64_t time = usfstl_sched_current_time(ctrl->sched);

	usfstl_sched_ctrl_request(ctrl->sched, time);
	usfstl_sched_ctrl_wait(ctrl->sched);
}

#define JOB_ASSERT_VAL(j) (j) ? (j)->name : "<NULL>"

static void _schedshm_setup_shared_mem(struct usfstl_sched_ctrl *ctrl, int *schedshm_fds)
{
	int memfd = schedshm_fds[UM_TIMETRAVEL_SHARED_MEMFD];

	/* make sure this is called only once */
	USFSTL_ASSERT_EQ(ctrl->shm.mem, NULL, "%p");
	ctrl->shm.mem = mmap(NULL, sizeof(ctrl->shm.mem->hdr),
			     PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
	USFSTL_ASSERT(ctrl->shm.mem != MAP_FAILED);
	ctrl->shm.mem = mremap(ctrl->shm.mem, sizeof(ctrl->shm.mem->hdr),
			       ctrl->shm.mem->len, MREMAP_MAYMOVE, NULL);
	USFSTL_ASSERT(ctrl->shm.mem);
	/*
	 * We need to dup the logfd due to it closed after callback is done,
	 * resulting in closing any FILE access to it
	 */
	ctrl->shm.flog = fdopen(dup(schedshm_fds[UM_TIMETRAVEL_SHARED_LOGFD]), "w");
	USFSTL_ASSERT(ctrl->shm.flog);
}

static uint64_t _schedctrl_get_time(struct usfstl_scheduler *sched)
{
	struct usfstl_sched_ctrl *ctrl = sched->ext.ctrl;
	uint64_t shared_time = ctrl->shm.mem->current_time;

	if (ctrl->frozen) {
		uint64_t local = ctrl->sched->current_time * ctrl->nsec_per_tick;

		ctrl->offset = shared_time - local;

		return ctrl->sched->current_time;
	}

	return DIV_ROUND_UP(shared_time - ctrl->offset, ctrl->nsec_per_tick);
}

static void _schedctrl_set_time(struct usfstl_scheduler *sched, uint64_t time)
{
	struct usfstl_sched_ctrl *ctrl = sched->ext.ctrl;
	uint64_t old_time = ctrl->shm.mem->current_time;
	uint64_t new_time = time * ctrl->nsec_per_tick + ctrl->offset;

	USFSTL_ASSERT(!ctrl->frozen);

	/* Only the running process can set the time */
	USFSTL_ASSERT_EQ(ctrl->shm.mem->running_id, ctrl->shm.id, "%d");

	DBG_SHAREDMEM(3, "new_time: %" PRIu64 ", free_until: %" PRIu64,
		      new_time, (uint64_t)ctrl->shm.mem->free_until);

	USFSTL_ASSERT_TIME_CMP(sched, new_time, >=, old_time);

	/* free until is upper limit for any time change by clients */
	USFSTL_ASSERT_TIME_CMP(sched, new_time, <=, ctrl->shm.mem->free_until);

	ctrl->shm.mem->current_time = new_time;
}

static enum usfstl_sched_req_status
_schedctrl_request_shm(struct usfstl_scheduler *sched, uint64_t time)
{
	struct usfstl_sched_ctrl *ctrl = sched->ext.ctrl;
	uint64_t req_time = time * ctrl->nsec_per_tick + ctrl->offset;
	union um_timetravel_schedshm_client *shm_self, *shm_running;

	if (!ctrl->started)
		return USFSTL_SCHED_REQ_STATUS_CAN_RUN;

	/*
	 * In case the running peer can't do shared mem we need to reach
	 * out to request the time via a message. If it can, we just need
	 * to update our request and free_until and the controller will
	 * put us on the schedule when the client finishes running.
	 *
	 * (If we're not waiting, obviously we have the capability.)
	 */
	shm_running = &ctrl->shm.mem->clients[ctrl->shm.mem->running_id];
	if (!(shm_running->capa & UM_TIMETRAVEL_SCHEDSHM_CAP_TIME_SHARE)) {
		usfstl_sched_ctrl_send_msg(ctrl, UM_TIMETRAVEL_REQUEST,
					   time * ctrl->nsec_per_tick + ctrl->offset);
		return USFSTL_SCHED_REQ_STATUS_WAIT;
	}

	DBG_SHAREDMEM(3, "req %" PRIu64 ", free_until %" PRIu64,
		      req_time, (uint64_t)ctrl->shm.mem->free_until);
	/* Make sure we are not requesting to run in the past */
	USFSTL_ASSERT_TIME_CMP(sched, req_time, >=, ctrl->shm.mem->current_time);

	if (usfstl_time_cmp(req_time, <, (uint64_t)ctrl->shm.mem->free_until)) {
		if (!ctrl->waiting)
			return USFSTL_SCHED_REQ_STATUS_CAN_RUN;
		/* if we're waiting, update free_until to new minimum */
		ctrl->shm.mem->free_until = req_time;
	}

	shm_self = &ctrl->shm.mem->clients[ctrl->shm.id];
	shm_self->req_time = req_time;
	shm_self->flags |= UM_TIMETRAVEL_SCHEDSHM_FLAGS_REQ_RUN;
	return USFSTL_SCHED_REQ_STATUS_WAIT;
}

static void _schedshm_cleanup(struct usfstl_sched_ctrl *ctrl)
{
	if (ctrl->shm.mem) {
		munmap(ctrl->shm.mem, sizeof(ctrl->shm.mem));
		ctrl->shm.mem = NULL;
		fclose(ctrl->shm.flog);
		ctrl->shm.flog = NULL;
	}
}

static bool _schedshm_handle_fds(struct usfstl_sched_ctrl *ctrl,
				 int schedshm_fds[], int nr_fds)
{
	_schedshm_setup_shared_mem(ctrl, schedshm_fds);

	if (ctrl->shm.mem->version != UM_TIMETRAVEL_SCHEDSHM_VERSION) {
		DBG_SHAREDMEM(0,
			      "No support for this sharedmem - expected version %d, version %d",
			      UM_TIMETRAVEL_SCHEDSHM_VERSION, ctrl->shm.mem->version);

		_schedshm_cleanup(ctrl);
		return true;
	}

	return false;
}

static void _sched_ctrl_ack_start_handle(struct usfstl_sched_ctrl *ctrl,
					 struct um_timetravel_msg *msg,
					 int *fds, int nr_fds)
{
	struct usfstl_scheduler *sched = ctrl->sched;
	uint64_t local;

	ctrl->shm.id = msg->time & UM_TIMETRAVEL_START_ACK_ID;

	/* only used once, unset it */
	ctrl->handle_msg_fds = NULL;
	USFSTL_ASSERT_EQ(nr_fds, (int)UM_TIMETRAVEL_SHARED_MAX_FDS, "%d");
	if (_schedshm_handle_fds(ctrl, fds, nr_fds))
		return;

	/*
	 * update offset now that we got to run, since in case of shared memory
	 * the controller cannot keep doing offset calculations for us
	 */
	local = usfstl_sched_current_time(ctrl->sched) * ctrl->nsec_per_tick;
	ctrl->offset = ctrl->shm.mem->current_time - local;

	sched->external_get_time = _schedctrl_get_time;
	sched->external_set_time = _schedctrl_set_time;
	sched->external_request = _schedctrl_request_shm;
	ctrl->shm.mem->clients[ctrl->shm.id].capa |= UM_TIMETRAVEL_SCHEDSHM_CAP_TIME_SHARE;
}

void usfstl_sched_ctrl_start(struct usfstl_sched_ctrl *ctrl,
			     const char *socket,
			     uint32_t nsec_per_tick,
			     uint64_t client_id,
			     struct usfstl_scheduler *sched)
{
	struct usfstl_job *job;

	USFSTL_ASSERT_EQ(ctrl->sched, NULL, "%p");
	USFSTL_ASSERT_EQ(sched->ext.ctrl, NULL, "%p");

	memset(ctrl, 0, sizeof(*ctrl));

	/*
	 * The remote side assumes we start at 0, so if we don't have 0 right
	 * now keep the difference in our own offset (in nsec).
	 */
	ctrl->offset = -usfstl_sched_current_time(sched) * nsec_per_tick;

	ctrl->nsec_per_tick = nsec_per_tick;
	ctrl->sched = sched;
	sched->ext.ctrl = ctrl;

	USFSTL_ASSERT_EQ(usfstl_sched_next_pending(sched, NULL),
			 (struct usfstl_job *)NULL, "%s", JOB_ASSERT_VAL);
	USFSTL_ASSERT_EQ(sched->external_request, NULL, "%p");
	USFSTL_ASSERT_EQ(sched->external_wait, NULL, "%p");

	sched->external_request = usfstl_sched_ctrl_request;
	sched->external_wait = usfstl_sched_ctrl_wait;

	ctrl->fd = usfstl_uds_connect(socket, usfstl_sched_ctrl_sock_read,
				      ctrl);

	/* until we get the ack we are in waiting state */
	ctrl->waiting = 1;
	ctrl->handle_msg_fds = _sched_ctrl_ack_start_handle;
	/* tell the other side we're starting  */
	usfstl_sched_ctrl_send_msg(ctrl, UM_TIMETRAVEL_START, client_id);
	ctrl->started = 1;
	/* now we're running until we send WAIT */
	ctrl->waiting = 0;

	/* if we have a job already, request it */
	job = usfstl_sched_next_pending(sched, NULL);
	if (job)
		usfstl_sched_ctrl_send_msg(ctrl, UM_TIMETRAVEL_REQUEST,
					   job->start * nsec_per_tick);

	/*
	 * At this point, we're allowed to do further setup work and can
	 * request schedule time etc. but must eventually start scheduling
	 * the linked scheduler - the remote side is blocked until we do.
	 */
}

void usfstl_sched_ctrl_sync_to(struct usfstl_sched_ctrl *ctrl)
{
	uint64_t time;

	USFSTL_ASSERT(ctrl->started, "cannot sync to scheduler until started");

	/* Any sync is done already by shared memory external_set_time */
	if (ctrl->shm.mem)
		return;

	time = usfstl_sched_current_time(ctrl->sched) * ctrl->nsec_per_tick;
	time += ctrl->offset;

	usfstl_sched_ctrl_send_msg(ctrl, UM_TIMETRAVEL_UPDATE, time);
}

void usfstl_sched_ctrl_sync_from(struct usfstl_sched_ctrl *ctrl)
{
	if (!ctrl->started)
		return;

	/* Any sync is done already by shared memory external_get_time */
	if (ctrl->shm.mem)
		return;

	usfstl_sched_ctrl_send_msg(ctrl, UM_TIMETRAVEL_GET, -1);
}

void usfstl_sched_ctrl_stop(struct usfstl_sched_ctrl *ctrl)
{
	USFSTL_ASSERT_EQ(ctrl, ctrl->sched->ext.ctrl, "%p");
	usfstl_uds_disconnect(ctrl->fd);

	_schedshm_cleanup(ctrl);

	ctrl->sched->ext.ctrl = NULL;
	ctrl->sched->external_request = NULL;
	ctrl->sched->external_wait = NULL;
	ctrl->sched->external_get_time = NULL;
	ctrl->sched->external_set_time = NULL;
	ctrl->sched = NULL;
}

void usfstl_sched_ctrl_set_frozen(struct usfstl_sched_ctrl *ctrl, bool frozen)
{
	ctrl->frozen = frozen;
}
