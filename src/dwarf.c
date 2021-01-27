/*
 * Copyright (C) 2018 - 2020 Intel Corporation
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
#include "internal.h"
#include "dwarf/backtrace.h"
#include "dwarf/internal.h"

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

#define for_each_unresolved_static_reference(p) \
	for (unsigned int i = 0; i < static_reference_count(); i++) \
		if ((p = __start_static_reference_data[i]) && (!(*p->ptr)))

static void usfstl_resolve_static_variable(const char *varname, const char *filename, void *varptr)
{
	const struct usfstl_static_reference *reference;

	for_each_unresolved_static_reference(reference) {
		if (reference->reference_type != USFSTL_STATIC_REFERENCE_VARIABLE)
			continue;

		if (strcmp(reference->name, varname))
			continue;

		if (strcmp(filename + strlen(filename) - strlen(reference->filename),
			   reference->filename)) {
			continue;
		}

		*reference->ptr = varptr;
		assert(*reference->ptr);
	}
}

static void usfstl_resolve_static_function(const char *filename, const char *varname)
{
	const struct usfstl_static_reference *reference;

	for_each_unresolved_static_reference(reference) {
		char *tp, *args;

		if (reference->reference_type != USFSTL_STATIC_REFERENCE_FUNCTION)
			continue;

		if (strcmp(reference->name, varname))
			continue;

		assert(dwarf_info_by_name(g_usfstl_backtrace_state, filename,
					  varname, reference->ptr, &tp, &args,
					  error_callback, NULL) == 0);
	}
}

static void usfstl_resolve_static_references(void)
{
	unsigned int n = &__stop_static_reference_data - __start_static_reference_data, i;
	const struct usfstl_static_reference *ref;
	uint32_t found = 0;
	bool all_resolved = true;

	/* check first if iterating dwarf data is worthwhile (takes a while) */
	for (i = 0; i < n; i++) {
		ref = __start_static_reference_data[i];
		if (!ref)
			continue;
		found |= 1 << ref->reference_type;
	}

	if (!found)
		return;

	if (found & (1 << USFSTL_STATIC_REFERENCE_VARIABLE))
		dwarf_iter_global_variables(g_usfstl_backtrace_state,
					    usfstl_resolve_static_variable,
					    error_callback, NULL);
	if (found & (1 << USFSTL_STATIC_REFERENCE_FUNCTION))
		dwarf_iter_functions(g_usfstl_backtrace_state,
				     usfstl_resolve_static_function,
				     error_callback, NULL);

	for (i = 0; i < n; i++) {
		ref = __start_static_reference_data[i];

		if (!ref)
			continue;
		if (*ref->ptr)
			continue;

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
	char *filename;
	char *funcname;
	unsigned int *lineno;
};

static int fileline_cb(void *_data, uintptr_t pc,
		       const char *filename, int lineno,
		       const char *function)
{
	struct func_data *data = _data;

	if (data->filename && filename)
		strcpy(data->filename, filename);
	if (data->funcname && function)
		strcpy(data->funcname, function);
	if (data->lineno)
		*data->lineno = lineno;
	return 0;
}

void usfstl_get_function_info(const void *fn, char *funcname,
			      char *filename, unsigned int *lineno)
{
	struct func_data data = {
		.filename = filename,
		.funcname = funcname,
		.lineno = lineno,
	};

	g_usfstl_fileline_fn(g_usfstl_backtrace_state, (intptr_t)fn, fileline_cb, error_callback, &data);
}

uintptr_t usfstl_dwarf_get_base_address(void)
{
	return dwarf_get_base_address(g_usfstl_backtrace_state);
}

void _usfstl_dump_stack(unsigned int skip)
{
	if (!g_usfstl_backtrace_state)
		return;

	backtrace_print(g_usfstl_backtrace_state, skip + 1, stderr);
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
