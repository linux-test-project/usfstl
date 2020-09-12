/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _USFSTL_SHARED_MEM_H_
#define _USFSTL_SHARED_MEM_H_

#define _SHARED_MEM_DUMMY_REF(line) usfstl_shared_mem_dummy_section_ref_##line
#define SHARED_MEM_DUMMY_REF(line) _SHARED_MEM_DUMMY_REF(line)

// variables defined using this macro will be synced between all participants
// for example: int USFSTL_SHARED_MEM_VAR(variable_name, SECTION_NAME);
// Note: the variables inside the section must be mapped in the same way for
// all usfstl participants.
// The macro also emits a dummy reference to the shared section definition,
// ensuring that it exists
#define USFSTL_SHARED_MEM_VAR(var, _section)					\
	var __attribute__((used, section("usfstl_shared_mem_" #_section)));	\
	extern const struct usfstl_shared_mem_section *				\
		usfstl_shared_mem_section_ ## _section;				\
	static void *SHARED_MEM_DUMMY_REF(__LINE__) __attribute__((unused)) =	\
		&usfstl_shared_mem_section_ ## _section;

// created by the linker
#define USFSTL_SHARED_MEM_START(section) __start_usfstl_shared_mem_ ## section
#define USFSTL_SHARED_MEM_STOP(section) __stop_usfstl_shared_mem_ ## section

typedef char usfstl_shared_mem_section_name_t[16];

/**
 * struct usfstl_shared_mem_section - simulation shared memory section
 * @name: name of the section - must be unique for each section
 * @p_start: section start address
 * @p_stop: section end address
 */
struct usfstl_shared_mem_section {
	usfstl_shared_mem_section_name_t name;
	char *p_start, *p_stop;
};

// define a memory section to be shared by all participants
// for example: USFSTL_SHARED_MEM_SECTION(SECTION_NAME);
// Every participant needs to use this macro in a single source file, per
// every section it needs to share.
// Each variable in a shared section is defined using USFSTL_SHARED_MEM_VAR.
// Note: the section pointer below is not static because it is referred by
// SHARED_MEM_DUMMY_REF. This also ensures that section names are unique
#define USFSTL_SHARED_MEM_SECTION(_name, ...)				\
extern char USFSTL_SHARED_MEM_START(_name)[];				\
extern char USFSTL_SHARED_MEM_STOP(_name)[];				\
static const struct usfstl_shared_mem_section _name ## _SECTION = {	\
	.name = #_name,							\
	.p_start = USFSTL_SHARED_MEM_START(_name),			\
	.p_stop = USFSTL_SHARED_MEM_STOP(_name),			\
	__VA_ARGS__							\
};									\
const struct usfstl_shared_mem_section *				\
usfstl_shared_mem_section_ ## _name					\
	__attribute__((used, section("usfstl_shms"))) = &_name ## _SECTION

#endif // _USFSTL_SHARED_MEM_H_
