/*
 * Copyright (C) 2019 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * This defines a simple RPC system for use with usfstl multi-process simulation.
 * RPC functions are called via their name, qualified with the return and
 * argument type. They can only have a single argument, for simplicity - use a
 * struct to fix that. Return types can be scalar or struct values as well.
 * Obviously, you cannot pass pointers as simulation components don't share an
 * address space.
 *
 * To use this, start with a header file that declares the functions you need,
 * e.g. rpc.h:
 *
 *	#if !defined(__RPC_H) || defined(USFSTL_RPC_CALLEE_STUB) || defined(USFSTL_RPC_CALLER_STUB)
 *	#define __RPC_H
 *	#include <usfstl/rpc.h>
 *	USFSTL_RPC_METHOD(int32_t, callme1, uint32_t);
 *	USFSTL_RPC_METHOD_P(int32_t, callme2, struct foo);
 *	USFSTL_RPC_VOID_METHOD(callme3, uint32_t);
 *	USFSTL_RPC_VOID_METHOD_P(callme4, struct foo);
 *	#endif // __RPC_H
 *
 * The following macros are supported:
 *	- USFSTL_RPC_METHOD()		- simple input/output types passed
 *					  by value
 *	- USFSTL_RPC_METHOD_P()		- input passed by reference instead
 *	- USFSTL_RPC_VOID_METHOD()
 *	- USFSTL_RPC_VOID_METHOD_P()	- similar but no return value
 *	- USFSTL_RPC_METHOD_VAR()	- variable-length input, simple output
 *	- USFSTL_RPC_VAR_METHOD()	- simple input passed by value,
 *					  variable output
 *	- USFSTL_RPC_VAR_METHOD_P()	- input passed by reference,
 *					  variable output
 *	- USFSTL_RPC_VAR_METHOD_VAR()	- variable input & output
 *
 * You must keep this file empty except for includes (which must have a double
 * include guard or #pragma once) and the USFSTL_RPC_METHOD (and variants) usage,
 * since this file is going to be included multiple times.
 * Finally, note that there's a limit on the length of the RPC name, which is a
 * concatenation of the return type, function name and argument type strings.
 *
 * Then, on the callee, you need to implement the stubs, e.g. in callee.c:
 *
 *	#include "rpc.h"
 *
 *	// magic incantation for usfstlrpc
 *	#define USFSTL_RPC_CALLEE_STUB
 *	#include <usfstl/rpc.h>
 *
 * And also the implementation of the functions, of course, in impl.c:
 *
 *      #include "rpc.h"
 *
 *	#define USFSTL_RPC_IMPLEMENTATION
 *	#include <usfstl/rpc.h>
 *
 *	USFSTL_RPC_METHOD(int32_t, callme1, uint32_t)
 *	{
 *		return in + 42;
 *	}
 *	USFSTL_RPC_METHOD_P(int32_t, callme2, struct foo)
 *	{
 *		return in->bar + 42;
 *	}
 *	USFSTL_RPC_VOID_METHOD(callme3, uint32_t)
 *	{
 *		// do something with "in"
 *		int something = in;
 *	}
 *	USFSTL_RPC_VOID_METHOD_P(callme4, struct foo)
 *	{
 *		// do something with "in"
 *		int something = in->bar;
 *	}
 *
 * Note that in addition to the 'in', 'insize' (for VAR in) and 'out'/'outsize'
 * (for VAR out) arguments, there's an implicit argument
 * 'struct usfstl_rpc_connection *conn' always passed, which indicates the caller
 * connection. Arbitrary data can be stored in the 'data' pointer there.
 *
 * On the caller, you don't have impl.c, instead you have stub.c:
 *
 *	#include <rpc.h>
 *
 *	// magic incantation for usfstlrpc
 *	#define USFSTL_RPC_CALLER_STUB
 *	#include <rpc.h>
 *
 * which declares the necessary stubs.
 *
 * Now you can transparently call the functions in any code that includes
 * rpc.h, regardless of whether it's local or remote, just call it:
 *
 *	printf("%d\n", callme1(100)); // prints "142"
 *
 *
 * Provision is made in the ABI to allow calling old methods (when the
 * return or argument type of a function changed (type or just size),
 * but currently no appropriate macros are provided for actually exposing
 * adjusted stubs.
 *
 * Also, currently no macros exist for argument-less functions (only
 * return-less functions), this can be added easily if needed.
 *
 *
 * Note also that this provides only the RPC system, the connection (consisting
 * of file descriptor(s)) to use for communication must be provided externally.
 *
 * Set the extern variable g_usfstl_rpc_default_connection to a connection struct
 * and then the calls made transparently to e.g. just "callme1(100)" as above
 * will use this default connection.
 * You also get a set of functions <name>_conn(), e.g. callme1_conn(), that use
 * a connection argument as the first argument. Using %NULL will use the default
 * (g_usfstl_rpc_default_connection), while using %USFSTL_RPC_LOCAL will make a
 * local call instead.
 *
 * For the callee, you must pass the connection to the usfstl_rpc_handle() when
 * it becomes readable.
 */
#if !defined(_USFSTL_RPC_H_) || \
    defined(USFSTL_RPC_CALLER_STUB) || \
    defined(USFSTL_RPC_CALLEE_STUB) || \
    defined(USFSTL_RPC_IMPLEMENTATION)
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include "list.h"
#include "loop.h"

#ifndef _USFSTL_RPC_H_

struct usfstl_rpc_connection {
	struct usfstl_list_entry entry;
	void *data;
	struct usfstl_loop_entry conn;
	uint32_t initialized:1,
	         broken:1;
	const char *name;

	/*
	 * extra data
	 *
	 * This data is transmitted in each RPC call made. Before sending
	 * the data, the extra_transmit() callback must provide it. When
	 * you receive it, the extra_received() callback is invoked.
	 */
	uint32_t extra_len;
	void (*extra_transmit)(struct usfstl_rpc_connection *, void *);
	void (*extra_received)(struct usfstl_rpc_connection *, const void *);

	void (*disconnected)(struct usfstl_rpc_connection *);
};

#define USFSTL_RPC_TAG_REQUEST	0x7573323e
#define USFSTL_RPC_TAG_RESPONSE	0x7573323c
#define USFSTL_VAR_DATA_SIZE	0x80000000

struct usfstl_rpc_request {
	char name[128];
	uint32_t retsize, argsize;
	/* followed by extra data */
	/* followed by argument data */
};

struct usfstl_rpc_response {
	uint32_t error;
};

struct usfstl_rpc_stub {
	struct usfstl_rpc_request req;
	void *fn;
};

void usfstl_rpc_call(struct usfstl_rpc_connection *conn, const char *name,
		     const void *arg, uint32_t argmin, uint32_t argsize,
		     void *ret, uint32_t retmin, uint32_t retsize);

void usfstl_rpc_add_connection(struct usfstl_rpc_connection *conn);
void usfstl_rpc_del_connection(struct usfstl_rpc_connection *conn);
void usfstl_rpc_handle(void);

extern struct usfstl_rpc_connection *g_usfstl_rpc_default_connection;

extern struct usfstl_rpc_connection g_usfstl_rpc_local;
#define USFSTL_RPC_LOCAL (&g_usfstl_rpc_local)
#endif
#define _USFSTL_RPC_H_

#undef _USFSTL_RPC_METHOD
#undef _USFSTL_RPC_VOID_METHOD
#undef _USFSTL_RPC_METHOD_VAR
#undef _USFSTL_RPC_VAR_METHOD
#undef _USFSTL_RPC_VAR_METHOD_VAR
#if defined(USFSTL_RPC_CALLEE_STUB)
/*
 * Define the callee stub, i.e. something that plugs into the RPC
 * system and lets a remote process call this function (by name);
 * we achieve this by registering in a global section as usual.
 */
#define _USFSTL_RPC_METHOD(_out, _name, _in, _p, _np, _d)		\
static void								\
_usfstl_rpc_stubfn_##_name(struct usfstl_rpc_connection *conn,		\
			   const _in *arg, void *ret)			\
{									\
	_out _ret = _impl_ ## _name(conn, _np arg);			\
	memcpy(ret, &_ret, sizeof(_out));				\
}									\
static const struct usfstl_rpc_stub usfstl_rpc_stub_##_name		\
__attribute__((section("usfstl_rpcstub"))) = {				\
	.req.name = #_name "-" #_out "-" #_in #_p,			\
	.req.argsize = sizeof(_in),					\
	.req.retsize = sizeof(_out),					\
	.fn = (void *)_usfstl_rpc_stubfn_##_name,			\
},									\
* const _usfstl_rpc_stub_##_name					\
__attribute__((section("usfstl_rpc"), used)) = &usfstl_rpc_stub_##_name
#define _USFSTL_RPC_VOID_METHOD(_name, _in, _p, _np, _d)		\
static void								\
_usfstl_rpc_stubfn_##_name(struct usfstl_rpc_connection *conn,		\
			   const _in *arg, void *ret)			\
{									\
	ret = ret; /* it is intentionally unused */			\
	_impl_ ## _name(conn, _np arg);					\
}									\
static const struct usfstl_rpc_stub usfstl_rpc_stub_##_name		\
__attribute__((section("usfstl_rpcstub"))) = {				\
	.req.name = #_name "--" #_in #_p,				\
	.req.argsize = sizeof(_in),					\
	.fn = (void *)_usfstl_rpc_stubfn_##_name,			\
},									\
* const _usfstl_rpc_stub_##_name					\
__attribute__((section("usfstl_rpc"), used)) = &usfstl_rpc_stub_##_name
#define _USFSTL_RPC_METHOD_VAR(_out, _name, _in)			\
static void								\
_usfstl_rpc_stubfn_##_name(struct usfstl_rpc_connection *conn,		\
			   const _in *arg,				\
			   const uint32_t argsz,			\
			   void *ret)					\
{									\
	_out _ret = _impl_ ## _name(conn, arg, argsz);			\
	memcpy(ret, &_ret, sizeof(_out));				\
}									\
static const struct usfstl_rpc_stub usfstl_rpc_stub_##_name		\
__attribute__((section("usfstl_rpcstub"))) = {				\
	.req.name = #_name "-" #_out "-" #_in "*",			\
	.req.argsize = USFSTL_VAR_DATA_SIZE | sizeof(_in),		\
	.req.retsize = sizeof(_out),					\
	.fn = (void *)_usfstl_rpc_stubfn_##_name,			\
},									\
* const _usfstl_rpc_stub_##_name					\
__attribute__((section("usfstl_rpc"), used)) = &usfstl_rpc_stub_##_name
#define _USFSTL_RPC_VAR_METHOD(_out, _name, _in, _p, _np, _d)		\
static void								\
_usfstl_rpc_stubfn_##_name(struct usfstl_rpc_connection *conn,		\
			   const _in *arg,				\
			   _out *ret,					\
			   const uint32_t retsz)			\
{									\
	_impl_ ## _name(conn, _np arg, ret, retsz);			\
}									\
static const struct usfstl_rpc_stub usfstl_rpc_stub_##_name		\
__attribute__((section("usfstl_rpcstub"))) = {				\
	.req.name = #_name "-" #_out "*-" #_in #_p,			\
	.req.argsize = sizeof(_in),					\
	.req.retsize = USFSTL_VAR_DATA_SIZE | sizeof(_out),		\
	.fn = (void *)_usfstl_rpc_stubfn_##_name,			\
},									\
* const _usfstl_rpc_stub_##_name					\
__attribute__((section("usfstl_rpc"), used)) = &usfstl_rpc_stub_##_name
#define _USFSTL_RPC_VAR_METHOD_VAR(_out, _name, _in)			\
static void								\
_usfstl_rpc_stubfn_##_name(struct usfstl_rpc_connection *conn,		\
			   const _in *arg,				\
			   const uint32_t argsz,			\
			   void *ret,					\
			   const uint32_t retsz)			\
{									\
	_impl_ ## _name(conn, arg, argsz, ret, retsz);			\
}									\
static const struct usfstl_rpc_stub usfstl_rpc_stub_##_name		\
__attribute__((section("usfstl_rpcstub"))) = {				\
	.req.name = #_name "-" #_out "*-" #_in "*",			\
	.req.argsize = USFSTL_VAR_DATA_SIZE | sizeof(_in),		\
	.req.retsize = USFSTL_VAR_DATA_SIZE | sizeof(_out),		\
	.fn = (void *)_usfstl_rpc_stubfn_##_name,			\
},									\
* const _usfstl_rpc_stub_##_name __attribute__((section("usfstl_rpc"),	\
					 used)) =			\
	&usfstl_rpc_stub_##_name
#elif defined(USFSTL_RPC_CALLER_STUB)
#define _USFSTL_RPC_METHOD(_out, _name, _in, _p, _np, _d)		\
_out _name(const _in _p arg)						\
{									\
	const _in _p _arg = arg;					\
	_out ret;							\
									\
	usfstl_rpc_call(g_usfstl_rpc_default_connection,		\
		      #_name "-" #_out "-" #_in #_p,			\
		      _d _arg, sizeof(_in), 0,				\
		      &ret, sizeof(_out), 0);				\
									\
	return ret;							\
}									\
_out _name ## _conn(struct usfstl_rpc_connection *conn,			\
		    const _in _p arg)					\
{									\
	const _in _p _arg = arg;					\
	_out ret;							\
									\
	if (!conn)							\
		conn = g_usfstl_rpc_default_connection;			\
									\
	usfstl_rpc_call(conn, #_name "-" #_out "-" #_in #_p,		\
		      _d _arg, sizeof(_in), 0, &ret, sizeof(_out), 0);	\
									\
	return ret;							\
}
#define _USFSTL_RPC_VOID_METHOD(_name, _in, _p, _np, _d)		\
void _name(const _in _p arg)						\
{									\
	const _in _p _arg = arg;					\
									\
	usfstl_rpc_call(g_usfstl_rpc_default_connection,		\
		      #_name "--" #_in #_p,				\
		      _d _arg, sizeof(_in), 0, NULL, 0, 0);		\
}									\
void _name ## _conn(struct usfstl_rpc_connection *conn,			\
		    const _in _p arg)					\
{									\
	const _in _p _arg = arg;					\
									\
	if (!conn)							\
		conn = g_usfstl_rpc_default_connection;			\
									\
	usfstl_rpc_call(conn, #_name "--" #_in #_p,			\
		      _d _arg, sizeof(_in), 0, NULL, 0, 0);		\
}
#define _USFSTL_RPC_METHOD_VAR(_out, _name, _in)			\
_out _name(const _in *arg, const uint32_t argsz)			\
{									\
	_out ret;							\
									\
	usfstl_rpc_call(g_usfstl_rpc_default_connection,		\
		      #_name "-" #_out "-" #_in "*",			\
		      arg, sizeof(_in), argsz | USFSTL_VAR_DATA_SIZE,	\
		      &ret, sizeof(_out), 0);				\
									\
	return ret;							\
}									\
_out _name ## _conn(struct usfstl_rpc_connection *conn,			\
		    const _in *arg, const uint32_t argsz)		\
{									\
	_out ret;							\
									\
	if (!conn)							\
		conn = g_usfstl_rpc_default_connection;			\
									\
	usfstl_rpc_call(conn, #_name "-" #_out "-" #_in "*",		\
		      arg, sizeof(_in), argsz | USFSTL_VAR_DATA_SIZE,	\
		      &ret, sizeof(_out), 0);				\
									\
	return ret;							\
}
#define _USFSTL_RPC_VAR_METHOD(_out, _name, _in, _p, _np, _d)		\
void _name(const _in _p arg, _out *ret, const uint32_t retsz)		\
{									\
	const _in _p _arg = arg;					\
									\
	usfstl_rpc_call(g_usfstl_rpc_default_connection,		\
		      #_name "-" #_out "*-" #_in #_p,			\
		      _d _arg, sizeof(_in), 0,				\
		      ret, sizeof(_out), retsz);			\
}									\
void _name ## _conn(struct usfstl_rpc_connection *conn,			\
		    const _in _p arg,					\
		    _out *ret, const uint32_t retsz)			\
{									\
	const _in _p _arg = arg;					\
									\
	if (!conn)							\
		conn = g_usfstl_rpc_default_connection;			\
									\
	usfstl_rpc_call(conn, #_name "-" #_out "*-" #_in #_p,		\
		      _d _arg, sizeof(_in), 0,				\
		      ret, sizeof(_out), retsz);			\
}
#define _USFSTL_RPC_VAR_METHOD_VAR(_out, _name, _in)			\
void _name(const _in *arg, const uint32_t argsz,			\
	   _out *ret, const uint32_t retsz)				\
{									\
	usfstl_rpc_call(g_usfstl_rpc_default_connection,		\
		      #_name "-" #_out "*-" #_in "*",			\
		      arg, sizeof(_in), argsz | USFSTL_VAR_DATA_SIZE,	\
		      ret, sizeof(_out), retsz);			\
}									\
void _name ## _conn(struct usfstl_rpc_connection *conn,			\
		    const _in *arg, const uint32_t argsz,		\
		    _out *ret, const uint32_t retsz)			\
{									\
	if (!conn)							\
		conn = g_usfstl_rpc_default_connection;			\
									\
	usfstl_rpc_call(conn, #_name "-" #_out "*-" #_in "*",		\
		      arg, sizeof(_in), argsz | USFSTL_VAR_DATA_SIZE,	\
		      ret, sizeof(_out), retsz);			\
}
#elif defined(USFSTL_RPC_IMPLEMENTATION)
#define _USFSTL_RPC_METHOD(_out, _name, _in, _p, _np, _d)		\
_out _impl_ ## _name(struct usfstl_rpc_connection *conn, const _in _p in)
#define _USFSTL_RPC_VOID_METHOD(_name, _in, _p, _np, _d)		\
void _impl_ ## _name(struct usfstl_rpc_connection *conn, const _in _p in)
#define _USFSTL_RPC_METHOD_VAR(_out, _name, _in)			\
_out _impl_ ## _name(struct usfstl_rpc_connection *conn,		\
		     const _in *in, const uint32_t insize)
#define _USFSTL_RPC_VAR_METHOD(_out, _name, _in, _p, _np, _d)		\
void _impl_ ## _name(struct usfstl_rpc_connection *conn,		\
		     const _in _p in,					\
		     _out *out, const uint32_t outsize)
#define _USFSTL_RPC_VAR_METHOD_VAR(_out, _name, _in)			\
void _impl_ ## _name(struct usfstl_rpc_connection *conn,		\
		     const _in *in, const uint32_t insize,		\
		     _out *out, const uint32_t outsize)
#else // normal include for just the prototypes
#define _USFSTL_RPC_METHOD(_out, _name, _in, _p, _np, _d)		\
_out _name ## _conn(struct usfstl_rpc_connection *, const _in _p in);	\
_out _name(const _in _p in);						\
_out _impl_ ## _name(struct usfstl_rpc_connection *, const _in _p in)
#define _USFSTL_RPC_VOID_METHOD(_name, _in, _p, _np, _d)		\
void _name ## _conn(struct usfstl_rpc_connection *, const _in _p in);	\
void _name(const _in _p in);						\
void _impl_ ## _name(struct usfstl_rpc_connection *, const _in _p in)
#define _USFSTL_RPC_METHOD_VAR(_out, _name, _in)			\
_out _name ## _conn(struct usfstl_rpc_connection *,			\
		    const _in *in, const uint32_t insize);		\
_out _name(const _in *in, const uint32_t insize);			\
_out _impl_ ## _name(struct usfstl_rpc_connection *,			\
		     const _in *in, const uint32_t insize)
#define _USFSTL_RPC_VAR_METHOD(_out, _name, _in, _p, _np, _d)		\
void _name ## _conn(struct usfstl_rpc_connection *,			\
		    const _in _p in,					\
		    _out *out, const uint32_t outsize);			\
void _name(const _in _p in,						\
	   _out *out, const uint32_t outsize);				\
void _impl_ ## _name(struct usfstl_rpc_connection *, const _in _p in,	\
		     _out *out, const uint32_t outsize)
#define _USFSTL_RPC_VAR_METHOD_VAR(_out, _name, _in)			\
void _name ## _conn(struct usfstl_rpc_connection *,			\
		    const _in *in, const uint32_t insize,		\
		    _out *out, const uint32_t outsize);			\
void _name(const _in *in, const uint32_t insize,			\
	   _out *out, const uint32_t outsize);				\
void _impl_ ## _name(struct usfstl_rpc_connection *,			\
		     const _in *in, const uint32_t insize,		\
		     _out *out, const uint32_t outsize)

#define USFSTL_RPC_METHOD(_out, _name, _in)				\
	_USFSTL_RPC_METHOD(_out, _name, _in,,*,&)
#define USFSTL_RPC_METHOD_P(_out, _name, _in)				\
	_USFSTL_RPC_METHOD(_out, _name, _in,*,,)
#define USFSTL_RPC_VOID_METHOD(_name, _in)				\
	_USFSTL_RPC_VOID_METHOD(_name, _in,,*,&)
#define USFSTL_RPC_VOID_METHOD_P(_name, _in)				\
	_USFSTL_RPC_VOID_METHOD(_name, _in,*,,)
#define USFSTL_RPC_METHOD_VAR(_out, _name, _in)				\
	_USFSTL_RPC_METHOD_VAR(_out, _name, _in)
#define USFSTL_RPC_VAR_METHOD(_out, _name, _in)				\
	_USFSTL_RPC_VAR_METHOD(_out, _name, _in,,*,&)
#define USFSTL_RPC_VAR_METHOD_P(_out, _name, _in)			\
	_USFSTL_RPC_VAR_METHOD(_out, _name, _in,*,,)
#define USFSTL_RPC_VAR_METHOD_VAR(_out, _name, _in)			\
	_USFSTL_RPC_VAR_METHOD_VAR(_out, _name, _in)
#endif // variants of prototype/code generation

#ifndef USFSTL_RPC_METHOD
#error "You must include your RPC header file w/o defining USFSTL_RPC_* first!"
#endif

/**
 * usfstl_rpc_send_void_response - send a void response out-of-band
 * @conn: the connection to send it to
 *
 * In some (rare) cases it may be necessary to send a response
 * to an RPC call out-of-band, e.g. after a longjmp(), see the
 * multi framework which requires this for an example.
 *
 * Try to avoid using this.
 */
void usfstl_rpc_send_void_response(struct usfstl_rpc_connection *conn);

#endif // _USFSTL_RPC_H_
