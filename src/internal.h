/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _USFSTL_INTERNAL_H_
#define _USFSTL_INTERNAL_H_
#include <stdarg.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdint.h>
#include <usfstl/test.h>
#include <usfstl/ctx.h>
#include <usfstl/rpc.h>
#include <usfstl/sched.h>

/* byteswap helper */
#define __swap32(v)			\
	((((v) & 0xff000000) >> 24) |	\
	 (((v) & 0x00ff0000) >>  8) |	\
	 (((v) & 0x0000ff00) <<  8) |	\
	 (((v) & 0x000000ff) << 24))

static inline uint32_t swap32(uint32_t v)
{
	return __swap32(v);
}

#define DIV_ROUND_UP(a, b) ({	\
	typeof(a) _a = a;	\
	typeof(b) _b = b;	\
	(_a + _b - 1) / _b;	\
})

/* main */
extern bool g_usfstl_list_tests;

/* print */
extern bool g_usfstl_flush_each_log;
void usfstl_vprintf(const char *msg, va_list ap);
void usfstl_flush_all(void);

/* entry */
#if defined(__CYGWIN__) || defined(_WIN32)
// in windows, assembler symbols are prefixed with _, but
// __fentry__ is an assembler symbol, so the first _ is
// the prefix and not seen from the C code.
#define __fentry__ _fentry__
#else
// similarly, we always use a _ prefix for these in the assembly,
// but the prefix doesn't disappear in Linux so #define it out
#define g_usfstl_recurse _g_usfstl_recurse
#define usfstl_find_repl _usfstl_find_repl
#endif

void __fentry__(void);
extern void *g_usfstl_recurse;

/* overrides */
void usfstl_reset_overrides(void);
const void *__attribute__((no_instrument_function, noinline))
usfstl_find_repl(const void *_orig);

/* dwarf */
void usfstl_dwarf_init(const char *self);
uintptr_t usfstl_dwarf_get_base_address(void);
void _usfstl_dump_stack(unsigned int skip);
int usfstl_get_func_info(const char *filename, const char *funcname,
			 const char **rettype, const char **args);

/* testrun */
extern bool g_usfstl_abort_on_error;
extern bool g_usfstl_test_aborted;
extern unsigned int g_usfstl_failure_reason;
void usfstl_out_of_time(void *rip);
enum usfstl_testcase_status usfstl_execute_test(const struct usfstl_test *tc,
						unsigned int test_num,
						unsigned int case_num,
						bool execute);
__attribute__((noreturn)) void usfstl_complete_abort(void);

/* watchdog */
extern bool g_usfstl_disable_wdt;
void usfstl_watchdog_start(unsigned int timeout_ms);
void usfstl_watchdog_stop(void);

/* save/restore */
void usfstl_save_globals(const char *program);
void usfstl_restore_globals(void);

/* test selection */
extern bool g_usfstl_skip_known_failing;

/* context */
/* must be first in struct usfstl_ctx */
struct usfstl_ctx_common {
	const char *name;
	void (*fn)(struct usfstl_ctx *, void *);
	void (*free)(struct usfstl_ctx *, void *);
	void *data;
	void *stack_start;

	struct usfstl_ctx_common *next;
};

void usfstl_ctx_common_init(struct usfstl_ctx *ctx, const char *name,
			    void (*fn)(struct usfstl_ctx *, void *),
			    void (*free)(struct usfstl_ctx *, void *),
			    void *data);
void usfstl_free_ctx_specific(struct usfstl_ctx *ctx);
void usfstl_free_ctx(struct usfstl_ctx *ctx);
void usfstl_ctx_cleanup(void);
void usfstl_ctx_free_main(void);

void usfstl_ctx_abort_test(void);

#define CTX_ASSERT_STR		"%p (%s)"
#define CTX_ASSERT_VAL(c)	(c), (c) ? usfstl_ctx_get_name(c) : "?"

/* scheduler */
void _usfstl_sched_set_time(struct usfstl_scheduler *sched, uint64_t time);

/* tasks */
void usfstl_task_cleanup(void);

/* rpc */
void rpc_write(usfstl_fd_t fd, const void *buf, size_t bufsize);
void rpc_read(usfstl_fd_t fd, void *buf, size_t nbyte);

/* multi-process testing */
extern struct usfstl_multi_participant *__start_usfstl_rpcp[];
extern struct usfstl_multi_participant *__stop_usfstl_rpcp[];
extern bool g_usfstl_multi_test_running;

#define for_each_participant(p, i)					\
	for (i = 0; &__start_usfstl_rpcp[i] < __stop_usfstl_rpcp; i++)	\
		if ((p = __start_usfstl_rpcp[i]))

void usfstl_multi_init(void);
void usfstl_multi_finish(void);

void usfstl_run_participant(struct usfstl_multi_participant *p, int nargs);

void usfstl_multi_start_test(void);
void usfstl_multi_start_test_controller(void);
void usfstl_multi_start_test_participant(void);
void usfstl_multi_end_test(enum usfstl_testcase_status status);
void usfstl_multi_end_test_controller(enum usfstl_testcase_status status);
void usfstl_multi_end_test_participant(void);

void usfstl_multi_sched_ext_req(struct usfstl_scheduler *sched, uint64_t at);

void usfstl_multi_controller_init(void);
int usfstl_multi_participant_run(void);

void usfstl_multi_extra_received(struct usfstl_rpc_connection *conn,
				 const void *data);

extern struct usfstl_rpc_connection *g_usfstl_multi_ctrl_conn;
extern bool g_usfstl_multi_test_sched_continue;

static inline bool usfstl_is_multi(void)
{
	return g_usfstl_multi_ctrl_conn;
}

static inline bool usfstl_is_multi_controller(void)
{
	return g_usfstl_multi_ctrl_conn == USFSTL_RPC_LOCAL;
}

static inline bool usfstl_is_multi_participant(void)
{
	return g_usfstl_multi_ctrl_conn &&
	       g_usfstl_multi_ctrl_conn != USFSTL_RPC_LOCAL;
}

/* shared memory */
extern struct usfstl_shared_mem_section *__start_usfstl_shms[];
extern struct usfstl_shared_mem_section *__stop_usfstl_shms[];

extern struct usfstl_shared_mem_msg *g_usfstl_shared_mem_msg;
extern bool g_usfstl_shared_mem_dirty;

#define for_each_shared_mem_section(s, i)				\
	for (i = 0; &__start_usfstl_shms[i] < __stop_usfstl_shms; i++)	\
		if ((s = __start_usfstl_shms[i]))

unsigned int usfstl_shared_mem_get_msg_size(bool is_participant_outdated);
void usfstl_shared_mem_handle_msg(const struct usfstl_shared_mem_msg *msg,
				  unsigned int msg_size);
void usfstl_shared_mem_update_local_view(void);
void usfstl_shared_mem_prepare_msg(bool do_not_mark_dirty);

extern char *g_usfstl_assert_coverage_file;
void usfstl_log_reached_asserts(void);
void usfstl_init_reached_assert_log(void);
void usfstl_list_all_asserts(void);

#if USFSTL_USE_FUZZING == 3
extern const unsigned char *g_usfstl_fuzz_data;
extern size_t g_usfstl_fuzz_datasz;
#endif

/* allocator */
void usfstl_free_all(void);

#endif // _USFSTL_INTERNAL_H_
