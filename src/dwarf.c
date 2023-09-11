/*
 * Copyright (C) 2018 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdlib.h>
#include <usfstl/test.h>
#include <usfstl/log.h>
#include "internal.h"
#include "dwarf/backtrace.h"
#include "dwarf/internal.h"
#include "dwarf-rpc.h"

/* created by the linker */
extern const struct usfstl_static_reference *__start_static_reference_data[];
extern const struct usfstl_static_reference *__stop_static_reference_data;

static unsigned int static_reference_count(void)
{
	return &__stop_static_reference_data - __start_static_reference_data;
}

/* need a dummy in case the test case has none */
static const struct usfstl_static_reference * const _dummy
__attribute__((used, section("static_reference_data"))) = NULL;

static struct backtrace_state *USFSTL_NORESTORE_VAR(g_usfstl_backtrace_state);
static fileline USFSTL_NORESTORE_VAR(g_usfstl_fileline_fn);

static void error_callback(void *data, const char *msg, int error_number)
{
	fprintf(stderr, "error %d: %s\n", error_number, msg);
	assert(0);
}

#define for_each_static_reference(p) \
	for (unsigned int i = 0; i < static_reference_count(); i++) \
		if ((p = __start_static_reference_data[i]))

#define for_each_unresolved_static_reference(p) \
	for (unsigned int i = 0; i < static_reference_count(); i++) \
		if ((p = __start_static_reference_data[i]) && (!(*p->ptr)))

static int usfstl_compare_paths(const char *s1, const char *s2)
{
#ifndef _WIN32
	return strcmp(s1, s2);
#else
	while (*s1 && *s2) {
		char c1 = *s1;
		char c2 = *s2;

		if ((c1 == '/' || c1 == '\\') && (c2 == '/' || c2 == '\\')) {
			s1++;
			s2++;
			continue;
		}

		if (c1 != c2)
			return c1 - c2;
		s1++;
		s2++;
	}

	return *s1 - *s2;
#endif
}

static void usfstl_resolve_static_variable(const char *varname, const char *filename, void *varptr)
{
	const struct usfstl_static_reference *reference;

	for_each_unresolved_static_reference(reference) {
		if (reference->reference_type != USFSTL_STATIC_REFERENCE_VARIABLE)
			continue;

		if (strcmp(reference->name, varname))
			continue;

		if (usfstl_compare_paths(filename + strlen(filename) - strlen(reference->filename),
			reference->filename)) {
			continue;
		}

		*reference->ptr = varptr;
		assert(*reference->ptr);
	}
}

struct usfstl_resolve_cbdata {
	const struct usfstl_static_reference *ref;
	char *ret;
	char *args;
};

static void usfstl_resolve_static_function(const char *filename,
					   const char *varname,
					   struct function *fn,
					   void *cbdata)
{
	struct usfstl_resolve_cbdata *cbd = cbdata;
	const struct usfstl_static_reference *reference = cbd->ref;
	void *ptr = NULL;

	if (reference->reference_type != USFSTL_STATIC_REFERENCE_FUNCTION)
		return;

	if (*reference->ptr && cbd->ret && cbd->args)
		return;

	dwarf_info_by_iterdata(g_usfstl_backtrace_state, fn, &ptr,
			       reference->filename ? &cbd->ret : NULL,
			       reference->filename ? &cbd->args : NULL,
			       error_callback, NULL);

	if (ptr)
		*reference->ptr = ptr;
}

static void usfstl_check_proto(const char *filename, const char *refname,
			       const char *orig_ret, const char *fptr_ret,
			       const char *orig_args, const char *fptr_args)
{
	unsigned int len = strlen(fptr_ret), pos = len - 1;
	char buf[len + 1];

	strcpy(buf, fptr_ret);

	USFSTL_ASSERT(buf[pos] == '*',
		      "usfstl malfunction: fptr ret must be pointer");
	buf[pos] = ' ';
	while (buf[pos] == ' ') {
		buf[pos] = 0;
		pos--;
	}

	if (strcmp(orig_ret, buf) == 0 && strcmp(orig_args, fptr_args) == 0)
		return;

	usfstl_printf("ERROR: %s: wrong prototype for static function %s\n",
		      filename, refname);
	usfstl_printf("original: %s %s(%s)\n", orig_ret, refname, orig_args);
	usfstl_printf("static:   %s %s(%s)\n", buf, refname, fptr_args);
	fflush(stdout);
	abort();
}

static void
usfstl_check_static_prototype(struct usfstl_resolve_cbdata *cb)
{
	const char *fptr_ret, *fptr_args;
	char fnbuf[25 + strlen(cb->ref->name)];

	/* this marks having a checker static function in this file */
	if (!cb->ref->filename)
		return;

	sprintf(fnbuf, "_usfstl_stf_proto_%s", cb->ref->name);
	USFSTL_ASSERT(usfstl_get_func_info(cb->ref->filename, fnbuf,
					   &fptr_ret, &fptr_args) == 0,
		      "Failed to get func info for %s (expected in file %s)",
		      fnbuf, cb->ref->filename);

	usfstl_check_proto(cb->ref->filename, cb->ref->name,
			   cb->ret ?: "void", fptr_ret,
			   cb->args ?: "void", fptr_args);
}

static void usfstl_resolve_static_references(void)
{
	const struct usfstl_static_reference *ref;
	uint32_t found = 0;
	bool all_resolved = true;

	/* check first if iterating dwarf data is worthwhile (takes a while) */
	for_each_unresolved_static_reference(ref)
		found |= 1 << ref->reference_type;

	if (!found)
		return;

	if (found & (1 << USFSTL_STATIC_REFERENCE_VARIABLE))
		dwarf_iter_global_variables(g_usfstl_backtrace_state,
					    usfstl_resolve_static_variable,
					    error_callback, NULL);

	for_each_static_reference(ref) {
		struct usfstl_resolve_cbdata cb = {
			.ref = ref,
		};

		if (ref->reference_type == USFSTL_STATIC_REFERENCE_FUNCTION)
			dwarf_iter_functions(g_usfstl_backtrace_state, ref->name,
					     usfstl_resolve_static_function, &cb,
					     error_callback, NULL);

		if (*ref->ptr) {
			if (ref->reference_type == USFSTL_STATIC_REFERENCE_FUNCTION)
				usfstl_check_static_prototype(&cb);
			continue;
		}

		fprintf(stderr, "usfstl static functions/variables: failed to resolve %s\n",
			ref->name);
		all_resolved = false;
	}

	assert(all_resolved);
}

void usfstl_dwarf_init(const char *self)
{
	int does_not_exist = -1;
	int descriptor = backtrace_open(self, error_callback, NULL, &does_not_exist);

	assert(does_not_exist == 0);

	g_usfstl_backtrace_state = backtrace_create_state(self, 1, error_callback, NULL);

	assert(backtrace_initialize(g_usfstl_backtrace_state, self, descriptor,
				    error_callback, NULL, &g_usfstl_fileline_fn) != -1);

	usfstl_resolve_static_references();
}

struct func_data {
	const char **filename;
	const char **funcname;
	unsigned int *lineno;
};

static int fileline_cb(void *_data, uintptr_t pc,
		       const char *filename, int lineno,
		       const char *function)
{
	struct func_data *data = _data;

	if (data->filename && filename)
		*data->filename = filename;
	if (data->funcname && function)
		*data->funcname = function;
	if (data->lineno)
		*data->lineno = lineno;
	return 0;
}

void usfstl_get_function_info_ptr(const void *fn, const char **funcname,
				  const char **filename, unsigned int *lineno)
{
	struct func_data data = {
		.filename = filename,
		.funcname = funcname,
		.lineno = lineno,
	};

	g_usfstl_fileline_fn(g_usfstl_backtrace_state, (intptr_t)fn, fileline_cb, error_callback, &data);
}

void usfstl_get_function_info(const void *ptr, char *funcname,
			      char *filename, unsigned int *lineno)
{
	const char *_funcname = NULL, *_filename = NULL;

	usfstl_get_function_info_ptr(ptr, &_funcname, &_filename, lineno);
	if (_funcname && funcname)
		strcpy(funcname, _funcname);
	if (_filename && filename)
		strcpy(filename, _filename);
}

uintptr_t usfstl_dwarf_get_base_address(void)
{
	return dwarf_get_base_address(g_usfstl_backtrace_state);
}

static bool g_usfstl_printed_dotdotdot;
static unsigned int g_usfstl_printed_rpc;
static bool g_usfstl_rpc_bt_initialized;
static unsigned int g_usfstl_rpc_bt_skip, g_usfstl_rpc_bt_skip_this;
static bool g_usfstl_rpc_bt_completed;

static int usfstl_bt_print_cb(void *data, uintptr_t pc, const char *filename,
			      int lineno, const char *function)
{
	if (!function) {
		if (!g_usfstl_printed_dotdotdot)
			fprintf(stderr, "...\n");
		g_usfstl_printed_dotdotdot = true;
		return 0;
	}

	g_usfstl_printed_dotdotdot = false;

	if (g_usfstl_rpc_bt_skip_this) {
		if (strcmp(function, "usfstl_rpc_call") == 0)
			g_usfstl_rpc_bt_skip_this--;
		return 0;
	}

	fprintf(stderr, "0x%lx %s\n\t%s:%d\n",
		(unsigned long) pc,
		function ?: "???",
		filename ?: "???",
		lineno);

	if (strcmp(function, "usfstl_rpc_handle_one_call") == 0) {
		if (!g_usfstl_rpc_bt_skip_this && g_usfstl_printed_rpc > 0) {
			struct usfstl_rpc_connection *conn;
			struct {
				struct usfstl_bt_name hdr;
				char name[200];
			} __attribute__((packed)) ret;

			conn = g_usfstl_rpc_stack[--g_usfstl_printed_rpc];

			fprintf(stderr, "\ncontinue backtrace on %s:\n\n",
				conn->name);
			fflush(stderr);

			// in case we get called again, skip this call
			g_usfstl_rpc_bt_skip++;
			rpc_bt_conn(conn, 0, &ret.hdr, sizeof(ret));
			if (g_usfstl_rpc_bt_completed)
				return 1;
			fprintf(stderr, "\nfinish backtrace on %.*s:\n\n",
				(int)(sizeof(ret) - sizeof(ret.hdr)), ret.name);
		} else if (g_usfstl_printed_rpc > 0) {
			g_usfstl_printed_rpc--;
		} else {
			fprintf(stderr,
				"\n!!! strange: mismatched RPC callee stack\n\n");
		}
	}

	return 0;
}

static void usfstl_bt_error_cb(void *data, const char *msg, int errnum)
{
	if (g_usfstl_backtrace_state->filename)
		fprintf(stderr, "%s: ", g_usfstl_backtrace_state->filename);

	fprintf(stderr, "libbacktrace: %s", msg);

	if (errnum > 0)
		fprintf(stderr, ": %s", strerror(errnum));

	fputc('\n', stderr);
}

static void usfstl_bt_init(unsigned int stack)
{
	unsigned int i;

	if (g_usfstl_rpc_bt_initialized)
		return;
	g_usfstl_rpc_bt_initialized = true;

	g_usfstl_printed_dotdotdot = false;
	g_usfstl_printed_rpc = stack;
	g_usfstl_rpc_bt_skip = 0;
	g_usfstl_rpc_bt_completed = false;

	for (i = 0; i < g_usfstl_printed_rpc; i++)
		rpc_bt_start_conn(g_usfstl_rpc_stack[i], 0);
}

void _usfstl_dump_stack(unsigned int skip)
{
	if (!g_usfstl_backtrace_state)
		return;

	g_usfstl_rpc_bt_initialized = false;
	usfstl_bt_init(g_usfstl_rpc_stack_num);

	backtrace_full(g_usfstl_backtrace_state, skip + 1,
		       usfstl_bt_print_cb, usfstl_bt_error_cb,
		       NULL);
}

void usfstl_dump_stack(void)
{
	_usfstl_dump_stack(0);
}

int usfstl_get_func_info(const char *filename, const char *funcname,
			 const char **rettype, const char **args)
{
	void *fnptr;

#ifdef _WIN32
	unsigned int i;
	char fwdfile[1000];

	if (filename) {
		for (i = 0; i < sizeof(fwdfile) - 1; i++) {
			if (!filename[i])
				break;
			if (filename[i] == '\\')
				fwdfile[i] = '/';
			else
				fwdfile[i] = filename[i];
		}
		fwdfile[i] = 0;
		filename = fwdfile;
	}
#endif

	if (dwarf_info_by_name(g_usfstl_backtrace_state, filename,
			       funcname, &fnptr, (char **)rettype,
			       (char **)args, error_callback, NULL) != 0)
		return -1;

	if (!*args)
		*args = "void";
	if (!*rettype)
		*rettype = "void";

	return 0;
}

#define USFSTL_RPC_CALLEE_STUB
#include "dwarf-rpc.h"
#undef USFSTL_RPC_CALLEE_STUB

#define USFSTL_RPC_CALLER_STUB
#include "dwarf-rpc.h"
#undef USFSTL_RPC_CALLER_STUB

#define USFSTL_RPC_IMPLEMENTATION
#include <usfstl/rpc.h>

USFSTL_RPC_VOID_METHOD(rpc_bt_start, uint32_t /* dummy */)
{
	usfstl_bt_init(g_usfstl_rpc_stack_num - 1);
}

USFSTL_RPC_VAR_METHOD(struct usfstl_bt_name, rpc_bt, uint32_t /* dummy */)
{
	g_usfstl_rpc_bt_initialized = false;

	g_usfstl_rpc_bt_skip++;
	g_usfstl_rpc_bt_skip_this = g_usfstl_rpc_bt_skip;

	backtrace_full(g_usfstl_backtrace_state, 1,
		       usfstl_bt_print_cb, usfstl_bt_error_cb,
		       NULL);
	g_usfstl_rpc_bt_completed = true;

	snprintf(out->name, outsize, "%s", conn->name);

	fflush(stderr);
}
