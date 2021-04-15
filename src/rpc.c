/*
 * Copyright (C) 2019 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <usfstl/test.h>
#include <usfstl/rpc.h>
#include <usfstl/list.h>
#include "internal.h"
#include "rpc-rpc.h"
#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#endif

struct usfstl_rpc_connection USFSTL_NORESTORE_VAR(g_usfstl_rpc_local) = {
#ifdef _WIN32
	.conn.fd = INVALID_SOCKET,
#else
	.conn.fd = -1,
#endif
};
struct usfstl_rpc_connection *USFSTL_NORESTORE_VAR(g_usfstl_rpc_default_connection) =
	USFSTL_RPC_LOCAL;
static struct usfstl_list USFSTL_NORESTORE_VAR(g_usfstl_rpc_connections) =
	USFSTL_LIST_INIT(g_usfstl_rpc_connections);
static uint32_t g_usfstl_rpc_wait_result;

// put a dummies into the sections to guarantee they're emitted
static const struct usfstl_rpc_stub * const dummy __attribute__((section("usfstl_rpc"), used)) =
	NULL;
static const unsigned int stub_fill __attribute__((section("usfstl_rpcstub"), used));

extern struct usfstl_rpc_stub *__start_usfstl_rpc[];
extern struct usfstl_rpc_stub *__stop_usfstl_rpc;

static void _usfstl_rpc_send_response(struct usfstl_rpc_connection *conn,
				      int status, const void *ret,
				      uint32_t retsize)
{
	static const uint32_t tag = USFSTL_RPC_TAG_RESPONSE;
	struct usfstl_rpc_response resp = {
		.error = status,
	};

	usfstl_flush_all();

	rpc_write(conn->conn.fd, &tag, sizeof(tag));
	rpc_write(conn->conn.fd, &resp, sizeof(resp));
	rpc_write(conn->conn.fd, ret, retsize);
}

static void usfstl_rpc_make_call(struct usfstl_rpc_connection *conn,
				 struct usfstl_rpc_stub *stub,
				 const void *arg, uint32_t argsize,
				 void *ret, uint32_t retsize)
{
	if (stub->req.argsize & USFSTL_VAR_DATA_SIZE &&
	    stub->req.retsize & USFSTL_VAR_DATA_SIZE) {
		void (*vfnv)(struct usfstl_rpc_connection *conn,
			     const void *, uint32_t,
			     void *, uint32_t) = stub->fn;

		vfnv(conn, arg, argsize, ret, retsize);
	} else if (stub->req.retsize & USFSTL_VAR_DATA_SIZE) {
		void (*vfn)(struct usfstl_rpc_connection *conn,
			    const void *, void *, uint32_t) = stub->fn;

		vfn(conn, arg, ret, retsize);
	} else if (stub->req.argsize & USFSTL_VAR_DATA_SIZE) {
		void (*fnv)(struct usfstl_rpc_connection *conn,
			    const void *, uint32_t, void *) = stub->fn;

		fnv(conn, arg, argsize, ret);
	} else {
		void (*fn)(struct usfstl_rpc_connection *conn,
			   const void *, void *) = stub->fn;

		fn(conn, arg, ret);
	}
}

static void usfstl_rpc_handle_call(struct usfstl_rpc_connection *conn,
				   struct usfstl_rpc_stub *stub,
				   uint32_t argsize, uint32_t retsize,
				   bool async)
{
	unsigned char arg[argsize]
		__attribute__((aligned(sizeof(uint64_t))));
	unsigned char ret[retsize]
		__attribute__((aligned(sizeof(uint64_t))));

	rpc_read(conn->conn.fd, arg, sizeof(arg));

	usfstl_rpc_make_call(conn, stub, arg, argsize, ret, retsize);

	if (async)
		return;

	_usfstl_rpc_send_response(conn, 0, ret, sizeof(ret));
}

static struct usfstl_rpc_stub *
usfstl_rpc_find_stub(struct usfstl_rpc_request *hdr, bool async)
{
	uint32_t argsize = hdr->argsize & ~USFSTL_VAR_DATA_SIZE;
	uint32_t retsize = hdr->retsize & ~USFSTL_VAR_DATA_SIZE;
	uint32_t fnidx;

	// Note: if this turns out to be slow, we can optimise it
	// on the first round into a binary tree or something...
	// But we'll probably only have a handful of entry points.
	for (fnidx = 0; &__start_usfstl_rpc[fnidx] < &__stop_usfstl_rpc; fnidx++) {
		struct usfstl_rpc_stub *stub = __start_usfstl_rpc[fnidx];

		if (!stub)
			continue;

		if (memcmp(&stub->req.name, hdr->name, sizeof(hdr->name)))
			continue;

		if (async != stub->async)
			continue;

		if ((hdr->argsize & USFSTL_VAR_DATA_SIZE) !=
				(stub->req.argsize & USFSTL_VAR_DATA_SIZE))
			continue;
		if (hdr->argsize & USFSTL_VAR_DATA_SIZE) {
			if (argsize < (stub->req.argsize & ~USFSTL_VAR_DATA_SIZE))
				continue;
		} else {
			if (argsize != stub->req.argsize)
				continue;
		}

		if ((hdr->retsize & USFSTL_VAR_DATA_SIZE) !=
				(stub->req.retsize & USFSTL_VAR_DATA_SIZE))
			continue;
		if (hdr->retsize & USFSTL_VAR_DATA_SIZE) {
			if (retsize < (stub->req.retsize & ~USFSTL_VAR_DATA_SIZE))
				continue;
		} else {
			if (retsize != stub->req.retsize)
				continue;
		}

		return stub;
	}

	return NULL;
}

static void usfstl_rpc_handle_one_call(struct usfstl_rpc_connection *conn,
				       struct usfstl_rpc_request *hdr,
				       bool async)
{
	struct usfstl_rpc_stub *stub;
	uint32_t argsize = hdr->argsize & ~USFSTL_VAR_DATA_SIZE;
	uint32_t retsize = hdr->retsize & ~USFSTL_VAR_DATA_SIZE;

	stub = usfstl_rpc_find_stub(hdr, async);
	if (stub) {
		usfstl_rpc_handle_call(conn, stub, argsize, retsize, async);
		return;
	}

	if (!async)
		_usfstl_rpc_send_response(conn, -ENOENT, NULL, 0);
}

static uint32_t usfstl_rpc_handle_one(struct usfstl_rpc_connection *conn)
{
	struct usfstl_rpc_request hdr;
	bool swap = false, async = false;
	uint32_t tag;

	rpc_read(conn->conn.fd, &tag, sizeof(tag));

	switch (tag) {
	case USFSTL_RPC_TAG_REQUEST:
		break;
	case USFSTL_RPC_TAG_ASYNC:
		async = true;
		break;
	case __swap32(USFSTL_RPC_TAG_REQUEST):
		swap = true;
		break;
	case __swap32(USFSTL_RPC_TAG_ASYNC):
		swap = true;
		async = true;
		break;
	case USFSTL_RPC_TAG_RESPONSE:
	case __swap32(USFSTL_RPC_TAG_RESPONSE):
		return tag;
	default:
		assert(0);
	}

	rpc_read(conn->conn.fd, &hdr, sizeof(hdr));

	if (swap) {
		hdr.retsize = swap32(hdr.retsize);
		hdr.argsize = swap32(hdr.argsize);
	}

	if (conn->extra_len) {
		unsigned char buf[conn->extra_len];

		rpc_read(conn->conn.fd, buf, conn->extra_len);

		conn->extra_received(conn, buf);
	}

	usfstl_rpc_handle_one_call(conn, &hdr, async);
	return 0;
}

static void usfstl_rpc_initialize(struct usfstl_rpc_connection *conn)
{
	struct usfstl_rpc_init init = {
		.extra_len = conn->extra_len,
	};

	conn->initialized = 1;
	rpc_init_conn(conn, &init, sizeof(init), &init, sizeof(init));
}

static void usfstl_rpc_loop_handler(struct usfstl_loop_entry *entry)
{
	struct usfstl_rpc_connection *conn;

	conn = container_of(entry, struct usfstl_rpc_connection, conn.list);

	assert(usfstl_rpc_handle_one(conn) == 0);
}

static void usfstl_rpc_loop_wait_handler(struct usfstl_loop_entry *entry)
{
	struct usfstl_rpc_connection *conn;
	uint32_t tag;

	conn = container_of(entry, struct usfstl_rpc_connection, conn.list);

	tag = usfstl_rpc_handle_one(conn);

	if (tag == USFSTL_RPC_TAG_RESPONSE ||
	    tag == __swap32(USFSTL_RPC_TAG_RESPONSE))
		g_usfstl_rpc_wait_result = tag;
}

void usfstl_rpc_add_connection(struct usfstl_rpc_connection *conn)
{
	if (!conn->initialized)
		usfstl_rpc_initialize(conn);
	conn->conn.handler = usfstl_rpc_loop_handler;
	usfstl_list_append(&g_usfstl_rpc_connections, &conn->entry);
	usfstl_loop_register(&conn->conn);
}

void usfstl_rpc_del_connection(struct usfstl_rpc_connection *conn)
{
	usfstl_loop_unregister(&conn->conn);
	usfstl_list_item_remove(&conn->entry);
	conn->initialized = 0;
}

static uint32_t usfstl_rpc_wait_and_handle(struct usfstl_rpc_connection *wait)
{
	uint32_t ret;

	do {
		unsigned int wait_prio;
		bool wait_registered;

		/*
		 * NOTE: we need to do this munging/unmunging inside the
		 *	 loop as we might recurse
		 */
		g_usfstl_rpc_wait_result = 0;

		if (wait) {
			wait_registered = wait->conn.list.next;
			if (wait_registered)
				usfstl_loop_unregister(&wait->conn);
			wait_prio = wait->conn.priority;
			wait->conn.priority = 0x7fffffff; // max
			wait->conn.handler = usfstl_rpc_loop_wait_handler;
			usfstl_loop_register(&wait->conn);
		}

		usfstl_loop_wait_and_handle();

		if (wait) {
			usfstl_loop_unregister(&wait->conn);
			wait->conn.priority = wait_prio;
			wait->conn.handler = usfstl_rpc_loop_handler;
			if (wait_registered)
				usfstl_loop_register(&wait->conn);
		}
	} while (wait && !g_usfstl_rpc_wait_result);

	ret = g_usfstl_rpc_wait_result;

	/* Restore it directly, in case we recursed */
	g_usfstl_rpc_wait_result = 0;

	return ret;
}

// callee implementation handling a single request
void usfstl_rpc_handle(void)
{
	assert(usfstl_rpc_wait_and_handle(NULL) == 0);
}

// caller implementation
static uint32_t usfstl_wait_for_response(struct usfstl_rpc_connection *conn)
{
	return usfstl_rpc_wait_and_handle(conn);
}

void usfstl_rpc_call(struct usfstl_rpc_connection *conn, const char *name,
		     const void *arg, uint32_t argmin, uint32_t argsize,
		     void *ret, uint32_t retmin, uint32_t retsize)
{
	struct usfstl_rpc_request req = {
		.argsize = argsize ?: argmin,
		.retsize = retsize ? retsize | USFSTL_VAR_DATA_SIZE : retmin,
	};
	struct usfstl_rpc_response resp;
	uint32_t tag = USFSTL_RPC_TAG_REQUEST;

	if (!conn->initialized)
		usfstl_rpc_initialize(conn);

	argsize &= ~USFSTL_VAR_DATA_SIZE;

	if (!argsize)
		argsize = argmin;
	if (!retsize)
		retsize = retmin;

	if (ret == USFSTL_RPC_ASYNC) {
		tag = USFSTL_RPC_TAG_ASYNC;
		ret = NULL;
	}

	// prepare request struct
	strcpy(req.name, name);

	if (conn == USFSTL_RPC_LOCAL) {
		struct usfstl_rpc_stub *stub;

		stub = usfstl_rpc_find_stub(&req, tag == USFSTL_RPC_TAG_ASYNC);
		// this would normally fail in other ways remotely, it would
		// so fail here directly
		assert(stub);

		if (conn->extra_len) {
			unsigned char buf[conn->extra_len];

			memset(&buf, 0, sizeof(buf));
			conn->extra_transmit(conn, buf);
			conn->extra_received(conn, buf);
		}

		usfstl_rpc_make_call(conn, stub, arg, argsize, ret, retsize);
		return;
	}

	usfstl_flush_all();

	// write request
	rpc_write(conn->conn.fd, &tag, sizeof(tag));
	rpc_write(conn->conn.fd, &req, sizeof(req));

	if (conn->extra_len) {
		unsigned char buf[conn->extra_len];

		memset(&buf, 0, sizeof(buf));
		conn->extra_transmit(conn, buf);

		rpc_write(conn->conn.fd, buf, conn->extra_len);
	}

	rpc_write(conn->conn.fd, arg, argsize);

	if (tag == USFSTL_RPC_TAG_ASYNC)
		return;

	tag = usfstl_wait_for_response(conn);

	// read response status
	rpc_read(conn->conn.fd, &resp, sizeof(resp));

	if (tag == swap32(USFSTL_RPC_TAG_RESPONSE))
		resp.error = swap32(resp.error);

	if (resp.error) {
		fprintf(stderr, "usfstl RPC call to %s failed, errno %d\n",
			name, resp.error);
		assert(0);
	}

	// read return value
	rpc_read(conn->conn.fd, ret, retsize);
}
