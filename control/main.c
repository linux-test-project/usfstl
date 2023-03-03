/*
 * Copyright (C) 2019 - 2023 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define _GNU_SOURCE 1 /* for struct ucred */
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <usfstl/sched.h>
#include <usfstl/loop.h>
#include <usfstl/opt.h>
#include <usfstl/uds.h>
#include <linux/um_timetravel.h>
#include <sys/socket.h>
#include "main.h"

static uint64_t clients;
#define CTRL_CLIENT_ID 0
#define CTRL_CLIENT_BIT(client_id) (1ULL << ((client_id) - 1))
#define CTRL_SCHEDSHM_MAX_CLIENTS (sizeof(clients) * 8)
static int expected_clients;
USFSTL_SCHEDULER(scheduler);
static struct usfstl_schedule_client *running_client;
static uint64_t time_at_start;
static int debug_level;
static int nesting;
static int process_start;
static struct um_timetravel_schedshm *g_schedshm_mem;
static int g_schedshm_fd_mem = -1;
static bool started_scheduling;
static USFSTL_LIST(client_list);

#define CLIENT_FMT "%s"
#define CLIENT_ARG(c) ((c)->name)

/* we don't want to link all of usfstl for this ... */
void usfstl_abort(const char *fn, unsigned int line,
		const char *cond, const char *msg, ...)
{
	va_list ap;

	fflush(stdout);
	fflush(stderr);
	fprintf(stderr, "in %s:%d\r\n", fn, line);
	fprintf(stderr, "condition %s failed\r\n", cond);
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	fprintf(stderr, "\r\n");
	va_end(ap);
	fflush(stderr);
	assert(0);
}

enum usfstl_schedule_client_state {
	USCS_CONNECTED = 0,
	USCS_START_REQUESTED,
	USCS_STARTED,
};

struct usfstl_schedule_client {
	struct usfstl_job job;
	struct usfstl_loop_entry conn;
	struct usfstl_list_entry list;
	enum usfstl_schedule_client_state state;
	bool sync_set;
	uint64_t sync;
	uint64_t n_req, n_wait, n_update;
	uint64_t last_message;
	uint64_t waiting_for;
	uint64_t offset;
	uint32_t start_seq;
	int nest;
	char name[40];
	uint64_t shm_name;
	uint8_t id;
	uint64_t pid;
};

static const char *opstr(int op)
{
#define OP(x) case UM_TIMETRAVEL_##x: return #x
	switch (op) {
	OP(ACK);
	OP(START);
	OP(REQUEST);
	OP(WAIT);
	OP(GET);
	OP(UPDATE);
	OP(RUN);
	OP(FREE_UNTIL);
	OP(GET_TOD);
	OP(BROADCAST);
	default:
		return "unknown op";
	}
}

static bool send_message(struct usfstl_schedule_client *client,
			 uint32_t op, uint64_t time);

static char *client_ts(struct usfstl_schedule_client *client)
{
	static char buf[21] = {};

	if (client->state != USCS_STARTED)
		return "tbd";

	if (!client->offset)
		return "=";

	sprintf(buf, "%"PRIu64,
		(uint64_t)usfstl_sched_current_time(&scheduler) - (client)->offset);

	return buf;
}

#define _DBG(lvl, fmt, ...)	do {					\
	 if (lvl <= debug_level) {					\
		printf("[%*d][%*" PRIu64 "]" fmt "\n",			\
		       2, nesting,					\
		       12, (uint64_t)usfstl_sched_current_time(&scheduler),\
		       ##__VA_ARGS__);					\
		fflush(stdout);						\
	}								\
} while (0)
#define DBG(lvl, fmt, ...) _DBG(lvl, " " fmt, ##__VA_ARGS__)

#define _DBG_CLIENT(lvl, client, fmt, ...)				\
	_DBG(lvl, "[%-20s %12s] " fmt,					\
	     (client)->name, client_ts(client), ##__VA_ARGS__)
#define _DBG_TXRX(lvl, client, rxtx, msg)				\
	_DBG_CLIENT(lvl, client, "%s%6d| %10s @ %" PRId64,		\
		    rxtx, (msg)->seq, opstr((msg)->op),			\
		    (int64_t)((msg)->time))
#define DBG_CLIENT(lvl, client, fmt, ...)				\
	_DBG_CLIENT(lvl, client, "         " fmt, ##__VA_ARGS__)
#define DBG_RX(lvl, client, msg)					\
	_DBG_TXRX(lvl, client, "<", msg)
#define DBG_TX(lvl, client, msg)					\
	_DBG_TXRX(lvl, client, ">", msg)

static void dump_sched(const char *msg)
{
	struct usfstl_job *job;

	if (debug_level < 3)
		return;

	DBG(3, "%s", msg);

	usfstl_sched_for_each_pending(&scheduler, job) {
		struct usfstl_schedule_client *client;

		printf("                  ");
		if (job->group == 1) {
			client = container_of(job, struct usfstl_schedule_client, job);

			printf("[%-20s %12s]", client->name, client_ts(client));
		} else {
			printf("[%-33s]", job->name);
		}
		printf("   prio:%u, start:%"PRIu64"\n", job->priority, job->start);
	}

	fflush(stdout);
}

static void free_client(struct usfstl_job *job)
{
	struct usfstl_schedule_client *client;

	client = container_of(job, struct usfstl_schedule_client, job);

	free(client);
}

static bool _schedshm_client_has_shm(uint16_t client_id)
{
	return g_schedshm_mem->clients[client_id].capa &
		UM_TIMETRAVEL_SCHEDSHM_CAP_TIME_SHARE;
}

/*
 * Set the running client, both our own pointer and the
 * shared memory running_id.
 */
static void set_running_client(struct usfstl_schedule_client *client)
{
	if (client)
		g_schedshm_mem->running_id = client->id;
	else
		/* controller runs on behalf of the client */
		g_schedshm_mem->running_id = CTRL_CLIENT_ID;

	running_client = client;
}

static void remove_client(struct usfstl_schedule_client *client)
{
	union um_timetravel_schedshm_client *shm_client;
	struct usfstl_job *job;

	usfstl_sched_del_job(&client->job);
	usfstl_loop_unregister(&client->conn);
	close(client->conn.fd);
	if (client->state == USCS_STARTED) {
		clients &= ~CTRL_CLIENT_BIT(client->id);
		usfstl_list_item_remove(&client->list);
	}

	/* remove from shared memory as well */
	shm_client = &g_schedshm_mem->clients[client->id];
	memset(shm_client, 0, sizeof(*shm_client));

	DBG_CLIENT(0, client,
		   "removed (req: %"PRIu64", wait: %"PRIu64", update: %"PRIu64")",
		   client->n_req, client->n_wait, client->n_update);

	/*
	 * Defer the free to the job callback, then we know we're no
	 * longer in any deep call stack where we might still access
	 * the data later.
	 * Note that we need to set the start time of this not to the
	 * current time but to the next already scheduled time if we
	 * have jobs pending, otherwise FREE_UNTIL messages might end
	 * up making time go backwards for some other client, i.e. we
	 * cannot make this job start any earlier than the next job
	 * that's already scheduled.
	 */
	job = usfstl_sched_next_pending(&scheduler, NULL);
	if (job)
		client->job.start = job->start;
	else
		client->job.start = usfstl_sched_current_time(&scheduler);
	client->job.callback = free_client;

	if (running_client == client)
		set_running_client(NULL);

	usfstl_sched_add_job(&scheduler, &client->job);
}

static bool write_message_fds(struct usfstl_schedule_client *client,
			      uint32_t op, uint32_t seq, uint64_t time,
			      const int *fds, unsigned int fds_num)
{
	struct um_timetravel_msg msg = {
		.op = op,
		.time = time,
		.seq = seq,
	};
	int ret;
	struct iovec iov = {
		.iov_base = &msg,
		.iov_len = sizeof(msg),
	};
	union {
		char control[CMSG_SPACE(sizeof(*fds) * UM_TIMETRAVEL_MAX_FDS)];
		struct cmsghdr align;
	} ctrl = {};
	struct msghdr msgh = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};

	/* if it's already being freed, don't try to send */
	if (client->job.callback == free_client)
		return false;

	DBG_TX(2, client, &msg);
	if (fds_num) {
		unsigned int fds_size = sizeof(*fds) * fds_num;
		struct cmsghdr *cmsg;

		USFSTL_ASSERT(fds_num <= UM_TIMETRAVEL_MAX_FDS,
			      "fds:%d > UM_TIMETRAVEL_MAX_FDS", fds_num);

		msgh.msg_control = ctrl.control;
		msgh.msg_controllen = CMSG_SPACE(fds_size);
		cmsg = CMSG_FIRSTHDR(&msgh);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(fds_size);
		memcpy(CMSG_DATA(cmsg), fds, fds_size);
	}

	ret = sendmsg(client->conn.fd, &msgh, 0);

	if (ret != sizeof(msg)) {
		remove_client(client);
		return false;
	}
	USFSTL_ASSERT_EQ(ret, (int)sizeof(msg), "%d");
	return true;
}

static bool write_message(struct usfstl_schedule_client *client,
			  uint32_t op, uint32_t seq, uint64_t time)
{
	return write_message_fds(client, op, seq, time, NULL, 0);
}

static void _schedshm_client_req_time(struct usfstl_schedule_client *client)
{
	uint16_t client_id = client->id;
	union um_timetravel_schedshm_client *shm_client;

	shm_client = &g_schedshm_mem->clients[client_id];
	if (!(shm_client->flags & UM_TIMETRAVEL_SCHEDSHM_FLAGS_REQ_RUN))
		return;

	/* don't move it to the end of the list if it's already scheduled */
	if (usfstl_job_scheduled(&client->job) &&
	    client->job.start == shm_client->req_time)
		return;

	usfstl_sched_del_job(&client->job);
	client->job.start = shm_client->req_time;
	usfstl_sched_add_job(&scheduler, &client->job);
	client->n_req++;
}

static uint32_t _handle_message(struct usfstl_schedule_client *client)
{
	struct um_timetravel_msg msg;
	uint64_t val = 0;
	int ret;

	ret = read(client->conn.fd, &msg, sizeof(msg));
	if (ret <= 0) {
		remove_client(client);
		return -1;
	}

	USFSTL_ASSERT_EQ(ret, (int)sizeof(msg), "%d");

	/* set up the client's name */
	if (msg.op == UM_TIMETRAVEL_START && msg.time != (uint64_t)-1) {
		DBG_CLIENT(2, client, "now known as id:%" PRIx64,
			   (uint64_t)msg.time);
		sprintf(client->name, "id:%" PRIx64, (uint64_t)msg.time);
		client->shm_name = msg.time;
	}

	if (client->waiting_for == msg.op)
		nesting--;
	DBG_RX(2, client, &msg);
	if (client->waiting_for == msg.op)
		nesting++;

	switch (msg.op) {
	case UM_TIMETRAVEL_ACK:
		return UM_TIMETRAVEL_ACK;
	case UM_TIMETRAVEL_REQUEST: {
		uint64_t req_time = client->offset + msg.time;

		USFSTL_ASSERT(client->state == USCS_STARTED,
			      "Client must not request runtime while not started!");

		/*
		 * Update shared memory on behalf of the client.
		 *
		 * Note we need to _always_ do this since it may also have a request
		 * in shared memory already and send a REQUEST, and later we then
		 * honour only the shared memory request.
		 */
		g_schedshm_mem->clients[client->id].flags |=
			UM_TIMETRAVEL_SCHEDSHM_FLAGS_REQ_RUN;
		g_schedshm_mem->clients[client->id].req_time = req_time;

		/*
		 * If the running client (including ourselves, since we don't
		 * handle shared memory free_until correctly yet) doesn't know
		 * about shared memory, then update scheduling immediately.
		 */
		if (!_schedshm_client_has_shm(g_schedshm_mem->running_id)) {
			usfstl_sched_del_job(&client->job);
			client->job.start = req_time;
			usfstl_sched_add_job(&scheduler, &client->job);
			/* adding job also updated free_until in shm */
		} else if (usfstl_time_cmp(req_time, <,
					   (uint64_t)g_schedshm_mem->free_until)) {
			g_schedshm_mem->free_until = req_time;
		}
		client->n_req++;
		}
		break;
	case UM_TIMETRAVEL_START:
		/*
		 * This is complicated. We need to set the client offset and
		 * so process the startup message not in some arbitrary loop
		 * handling place (like we're in here).
		 * We also cannot interact with the scheduler here (and add
		 * a job to it for processing the startup) because that would
		 * require that we update any currently running client with a
		 * new FREE_UNTIL, which is unsafe anyway as it might be past
		 * that point already.
		 *
		 * Thus, we just set the "process_start" flag here and later
		 * call process_starting_clients() in the right place on the
		 * outermost scheduling layer, including sending the ACK.
		 */
		process_start = true;
		client->start_seq = msg.seq;
		client->state = USCS_START_REQUESTED;
		return UM_TIMETRAVEL_START;
	case UM_TIMETRAVEL_WAIT:
		USFSTL_ASSERT(client == running_client || !running_client,
			      "Client must not wait while not running!");
		client->n_wait++;
		if (running_client) {
			USFSTL_ASSERT_EQ(running_client, client,
					 CLIENT_FMT, CLIENT_ARG);
			set_running_client(NULL);
		}

		/* In shared memory mode we don't wait send ack on wait message
		 * as required by linux/um_timetravel.h
		 */
		if (_schedshm_client_has_shm(client->id))
			return UM_TIMETRAVEL_WAIT;
		break;
	case UM_TIMETRAVEL_GET:
		USFSTL_ASSERT(client->state == USCS_STARTED,
			      "Client must not retrieve time while not started!");
		val = usfstl_sched_current_time(&scheduler) - client->offset;
		break;
	case UM_TIMETRAVEL_GET_TOD:
		USFSTL_ASSERT(client->state == USCS_STARTED,
			      "Client must not retrieve TOD while not started!");
		val = time_at_start + usfstl_sched_current_time(&scheduler);
		break;
	case UM_TIMETRAVEL_UPDATE:
		USFSTL_ASSERT(client == running_client,
			      "Client must not update time while not running!");
		usfstl_sched_set_time(&scheduler, client->offset + msg.time);
		client->n_update++;
		break;
	case UM_TIMETRAVEL_BROADCAST: {
		struct usfstl_loop_entry *entry, *tmp;

		DBG_CLIENT(3, client, "Got BROADCAST message %llx", msg.time);
		/* we need to use safe due to change the list while waiting for ack */
		usfstl_loop_for_each_entry_safe(entry, tmp) {
			struct usfstl_schedule_client *other_client;
			other_client = container_of(entry, struct usfstl_schedule_client, conn);

			/* To be on the safe side only send to started clients */
			if (other_client->state != USCS_STARTED)
				continue;

			/* Don't send the message to whom sent the message */
			if (other_client == client)
				continue;

			send_message(other_client, UM_TIMETRAVEL_BROADCAST, msg.time);
		}
		break;
	}
	case UM_TIMETRAVEL_RUN:
	case UM_TIMETRAVEL_FREE_UNTIL:
		DBG_CLIENT(0, client, "invalid message %"PRIu32,
			   (uint32_t)msg.op);
		remove_client(client);
		return -1;
	}

	write_message(client, UM_TIMETRAVEL_ACK, msg.seq, val);

	return msg.op;
}

static void handle_message(struct usfstl_loop_entry *conn)
{
	struct usfstl_schedule_client *client;

	client = container_of(conn, struct usfstl_schedule_client, conn);

	USFSTL_ASSERT_CMP(_handle_message(client), != ,
			(uint32_t)UM_TIMETRAVEL_ACK, "%" PRIu32);
}

static void handle_message_wait(struct usfstl_loop_entry *conn)
{
	struct usfstl_schedule_client *client;
	uint32_t op;

	client = container_of(conn, struct usfstl_schedule_client, conn);

	op = _handle_message(client);
	client->last_message = op;
}

static void wait_for(struct usfstl_schedule_client *wait, uint32_t op)
{
	nesting++;

	do {
		unsigned int wait_prio;
		void (*wait_registered)(struct usfstl_loop_entry *) = NULL;

		/*
		 * NOTE: we need to do this munging/unmunging inside the
		 *	 loop as we might recurse
		 */
		wait->last_message = -2;
		wait->waiting_for = op;

		if (wait->conn.list.next)
			wait_registered = wait->conn.handler;
		if (wait_registered)
			usfstl_loop_unregister(&wait->conn);
		wait_prio = wait->conn.priority;
		wait->conn.priority = 0x7fffffff; // max
		wait->conn.handler = handle_message_wait;
		usfstl_loop_register(&wait->conn);

		usfstl_loop_wait_and_handle();

		/* could have been removed, so check if it's still there */
		if (wait->conn.list.next && wait_registered == handle_message) {
			usfstl_loop_unregister(&wait->conn);
			wait->conn.priority = wait_prio;
			wait->conn.handler = handle_message;
			if (wait_registered)
				usfstl_loop_register(&wait->conn);
		} else {
			break;
		}
	} while (wait->last_message != op);

	/* Restore it directly, in case we recursed */
	wait->last_message = -2;
	wait->waiting_for = -1;
	nesting--;
}

static bool send_message(struct usfstl_schedule_client *client,
			 uint32_t op, uint64_t time)
{
	static uint64_t seq;

	seq++;
	client->nest++;
	if (!write_message(client, op, seq, time))
		return false;
	/* In shared memory mode we don't wait for ack on run as required by
	 * linux/um_timetravel.h
	 */
	if (op != UM_TIMETRAVEL_RUN || !_schedshm_client_has_shm(client->id))
		wait_for(client, UM_TIMETRAVEL_ACK);
	client->nest--;
	return true;
}

static void update_sync(struct usfstl_schedule_client *client)
{
	uint64_t sync = usfstl_sched_get_sync_time(&scheduler);
	static bool update_sync_running;

	if (!client)
		client = running_client;
	else
		set_running_client(client);

	if (!started_scheduling)
		return;

	g_schedshm_mem->free_until = sync;

	if (!client)
		return;

	/* client can read directly the free_until value */
	if (_schedshm_client_has_shm(client->id))
		return;

	// If we synced it to exactly the same time before, don't do it again.
	if (client->sync_set && client->sync == sync)
		return;

	dump_sched("sync update");

	if (update_sync_running)
		return;

	update_sync_running = true;

	send_message(client, UM_TIMETRAVEL_FREE_UNTIL, sync - client->offset);
	client->sync_set = true;
	client->sync = sync;

	update_sync_running = false;
}

static void process_starting_client(struct usfstl_schedule_client *client)
{
	union um_timetravel_schedshm_client *mem_client;
	int schedshm_fds[UM_TIMETRAVEL_SHARED_MAX_FDS] = {
		[UM_TIMETRAVEL_SHARED_MEMFD] = g_schedshm_fd_mem,
		[UM_TIMETRAVEL_SHARED_LOGFD] = fileno(stdout),
	};

	client->offset = usfstl_sched_current_time(&scheduler);
	client->state = USCS_STARTED;
	client->id = __builtin_ffsll(~clients);
	/* If you hit this assert and have the need for move than 64
	 * clients, it can be handled by an array of clients bits.
	 */
	USFSTL_ASSERT(client->id, "Got to max clients we can handle");
	mem_client = &g_schedshm_mem->clients[client->id];
	mem_client->name = client->shm_name;
	clients |= CTRL_CLIENT_BIT(client->id);
	usfstl_list_append(&client_list, &client->list);

	set_running_client(client);

	// assert that the ID can be sent as part of the message
	USFSTL_ASSERT_EQ((uint64_t)client->id & ~UM_TIMETRAVEL_START_ACK_ID,
			 (uint64_t)0, "%" PRIu64);
	write_message_fds(client, UM_TIMETRAVEL_ACK, client->start_seq,
			  client->id & UM_TIMETRAVEL_START_ACK_ID,
			  schedshm_fds, UM_TIMETRAVEL_SHARED_MAX_FDS);
	wait_for(client, UM_TIMETRAVEL_WAIT);
}

static void process_starting_clients(void)
{
	struct usfstl_loop_entry *entry, *tmp;

	// Note: need to loop since we might get a new start message
	// from another new client while we wait for the WAIT message
	// from this one in process_starting_client().
	while (process_start) {
		// We need _safe since we could remove an entry as well.
		usfstl_loop_for_each_entry_safe(entry, tmp) {
			struct usfstl_schedule_client *client;

			// Note: we do mangle this sometimes, but here we are outside
			// of the scheduling, so can't be in a wait_for()
			if (entry->handler != handle_message)
				continue;

			client = container_of(entry, struct usfstl_schedule_client, conn);

			if (client->state != USCS_START_REQUESTED)
				continue;

			process_start = false;
			process_starting_client(client);
			// start again from the beginning if we have a new one,
			// to avoid missing one ...
			if (process_start)
				break;
		}
	}
}

static void run_client(struct usfstl_job *job)
{
	struct usfstl_schedule_client *client;

	client = container_of(job, struct usfstl_schedule_client, job);

	DBG_CLIENT(2, client, "running");

	update_sync(client);

	/* shared mem clients will clear it themselves, but others can't */
	if (!_schedshm_client_has_shm(client->id))
		g_schedshm_mem->clients[client->id].flags &=
			~UM_TIMETRAVEL_SCHEDSHM_FLAGS_REQ_RUN;

	if (send_message(client, UM_TIMETRAVEL_RUN,
			 usfstl_sched_current_time(&scheduler) - client->offset))
		wait_for(client, UM_TIMETRAVEL_WAIT);
}

static void handle_new_connection(int fd, void *data)
{
	struct usfstl_schedule_client *client;
	static int ctr;
	struct ucred ucred;
	socklen_t ucred_sz = sizeof(ucred);
	int ret;

	client = malloc(sizeof(*client));
	assert(client);
	memset(client, 0, sizeof(*client));

	client->conn.fd = fd;
	client->conn.handler = handle_message;
	client->job.name = client->name;
	sprintf(client->name, "unnamed-%d", ++ctr);
	client->job.callback = run_client;
	client->job.group = 1;
	usfstl_loop_register(&client->conn);
	client->waiting_for = -1;

	ret = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &ucred_sz);
	if (ret == 0) {
		client->pid = ucred.pid;
		DBG_CLIENT(0, client, "connected (pid=%d)", ucred.pid);
	} else {
		DBG_CLIENT(0, client, "connected");
	}
}

static void _schedshm_set_time(struct usfstl_scheduler *sched, uint64_t time)
{
	/* make sure we are in time control now */
	USFSTL_ASSERT(!_schedshm_client_has_shm(g_schedshm_mem->running_id));
	/* control time is always equal shared time */
	g_schedshm_mem->current_time = time;
}

static uint64_t _schedshm_get_time(struct usfstl_scheduler *sched)
{
	/* control time is always equal shared time */
	return g_schedshm_mem->current_time;
}

static void _schedshm_create_mem_file(void)
{
	const char *name = "schedshm";
	const int mem_size = sizeof(*g_schedshm_mem) +
		sizeof(*g_schedshm_mem->clients) * CTRL_SCHEDSHM_MAX_CLIENTS;

	/* make sure this is called only once */
	USFSTL_ASSERT_EQ(g_schedshm_fd_mem, -1, "%d");
	USFSTL_ASSERT_EQ(g_schedshm_mem, NULL, "%p");

	g_schedshm_fd_mem = memfd_create(name, MFD_ALLOW_SEALING);
	USFSTL_ASSERT(g_schedshm_fd_mem >= 0, "failed to create memfd %s\n", name);

	/* Inc the file size to requested len */
	USFSTL_ASSERT_EQ(ftruncate(g_schedshm_fd_mem, mem_size), 0, "%d");

	/* Seal the file for any grow/shrink changes */
	USFSTL_ASSERT_EQ(fcntl(g_schedshm_fd_mem, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK),
			 0, "%d");
	g_schedshm_mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
			      MAP_SHARED, g_schedshm_fd_mem, 0);
	USFSTL_ASSERT(g_schedshm_mem != MAP_FAILED);
	g_schedshm_mem->len = mem_size;
	g_schedshm_mem->max_clients = CTRL_SCHEDSHM_MAX_CLIENTS;
	g_schedshm_mem->version = UM_TIMETRAVEL_SCHEDSHM_VERSION;

	/* set up sched related fields */
	g_schedshm_mem->current_time = scheduler.current_time;
	g_schedshm_mem->free_until = scheduler.current_time;
	USFSTL_ASSERT_EQ(scheduler.external_get_time, NULL, "%p");
	scheduler.external_get_time = _schedshm_get_time;
	USFSTL_ASSERT_EQ(scheduler.external_set_time, NULL, "%p");
	scheduler.external_set_time = _schedshm_set_time;
}

static char *path;
USFSTL_OPT_STR("time", 't', "socket", path, "socket for time protocol");

USFSTL_OPT_INT("clients", 'c', "clients", expected_clients, "# of clients");

bool wallclock_network;
USFSTL_OPT_FLAG("wallclock-network", 0, wallclock_network,
		"Enable wallclock-network mode, mutually exclusive with time socket\n"
"                 and # of clients, must kill the program by force in this mode.");
USFSTL_OPT_INT("debug", 0, "level", debug_level, "debug level");
USFSTL_OPT_U64("time-at-start", 0, "opt_time_at_start", time_at_start,
	       "set the start time");

static void next_time_changed(struct usfstl_scheduler *sched)
{
	update_sync(NULL);
}

int main(int argc, char **argv)
{
	int ret = usfstl_parse_options(argc, argv);
	struct usfstl_schedule_client *tmp;

	if (ret)
		return ret;

	signal(SIGPIPE, SIG_IGN);

	USFSTL_ASSERT(path || wallclock_network,
		      "must have a socket path or wallclock network mode");
	USFSTL_ASSERT(!wallclock_network || !expected_clients,
		      "must not have --clients in wallclock network mode");

	if (!time_at_start) {
		struct timespec wallclock;

		clock_gettime(CLOCK_REALTIME, &wallclock);
		time_at_start = wallclock.tv_nsec +
				wallclock.tv_sec * 1000*1000*1000;
	}

	net_init();
	if (path)
		usfstl_uds_create(path, handle_new_connection, NULL);

	scheduler.next_time_changed = next_time_changed;

	/* Call this after scheduler is initialized to get valid current time
	 * from scheduler
	 */
	_schedshm_create_mem_file();

	usfstl_sched_start(&scheduler);
	if (wallclock_network)
		usfstl_sched_wallclock_init(&scheduler, 1);

	DBG(0, "waiting for %d clients", expected_clients);

	while (__builtin_popcountll(clients) < expected_clients) {
		usfstl_loop_wait_and_handle();
		process_starting_clients();
	}

	DBG(0, "have %d clients now", __builtin_popcountll(clients));

	usfstl_for_each_list_item(tmp, &client_list, list)
		_schedshm_client_req_time(tmp);
	started_scheduling = true;

	while (wallclock_network) {
		usfstl_sched_wallclock_wait_and_handle(&scheduler);
		if (usfstl_sched_next_pending(&scheduler, NULL)) {
			dump_sched("schedule");
			usfstl_sched_next(&scheduler);
		}

		process_starting_clients();
		usfstl_for_each_list_item(tmp, &client_list, list)
			_schedshm_client_req_time(tmp);
	}

	while (clients && usfstl_sched_next_pending(&scheduler, NULL)) {
		dump_sched("schedule");
		usfstl_sched_next(&scheduler);
		process_starting_clients();
		usfstl_for_each_list_item(tmp, &client_list, list)
			_schedshm_client_req_time(tmp);
	}

	usfstl_uds_remove(path);
	net_exit();

	return 0;
}
