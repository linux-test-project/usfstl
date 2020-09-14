/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <ucontext.h>
#include <usfstl/test.h>
#include <usfstl/ctx.h>
#include <sanitizer/asan_interface.h>
#include <stdbool.h>
#include "internal.h"

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#if !__has_feature(address_sanitizer) && !defined(__SANITIZE_ADDRESS__)
#define __sanitizer_start_switch_fiber __noasan__sanitizer_start_switch_fiber
#define __sanitizer_finish_switch_fiber __noasan__sanitizer_finish_switch_fiber
static void __sanitizer_start_switch_fiber(void **fake_stack_save,
					   const void *bottom, size_t size)
{
}
static void __sanitizer_finish_switch_fiber(void *fake_stack_save,
					    const void **bottom_old,
					    size_t *size_old)
{
}
#endif

static struct usfstl_ctx *g_usfstl_current_ctx;
static struct usfstl_ctx *g_usfstl_destroy_ctx;
static struct usfstl_ctx *g_usfstl_destroy_caller;
static struct usfstl_ctx *g_usfstl_main_ctx;

struct usfstl_ctx {
	/* must be first */
	struct usfstl_ctx_common common;

	ucontext_t ctx;

	struct {
		const void *bottom;
		size_t size;
	} asan;
};

void usfstl_free_ctx_specific(struct usfstl_ctx *ctx)
{
	free(ctx->ctx.uc_stack.ss_sp);
	free(ctx);
}

static void _usfstl_complete_ctx_switch(struct usfstl_ctx *ctx, void *prev_stack)
{
	__sanitizer_finish_switch_fiber(prev_stack,
					&g_usfstl_current_ctx->asan.bottom,
					&g_usfstl_current_ctx->asan.size);

	g_usfstl_current_ctx = ctx;

	if (g_usfstl_destroy_caller) {
		struct usfstl_ctx *destroyer = g_usfstl_destroy_caller;

		g_usfstl_destroy_caller = NULL;
		usfstl_end_self(destroyer);
		// doesn't return
	}

	if (g_usfstl_destroy_ctx) {
		// free the ctx data we came from if it asked for it
		usfstl_free_ctx(g_usfstl_destroy_ctx);
		g_usfstl_destroy_ctx = NULL;
	}
}

static void _usfstl_context_fn(struct usfstl_ctx *ctx)
{
	char dummy;

	// set current ctx and stack start when we start running
	_usfstl_complete_ctx_switch(ctx, NULL);

	usfstl_set_stack_start(&dummy);
	ctx->common.fn(ctx, ctx->common.data);

	usfstl_end_self(g_usfstl_main_ctx);
}

struct usfstl_ctx *
usfstl_ctx_create(const char *name,
		  void (*fn)(struct usfstl_ctx *ctx, void *data),
		  void (*free)(struct usfstl_ctx *ctx, void *data),
		  void *data)
{
	struct usfstl_ctx *ctx = calloc(1, sizeof(*ctx));

	// when creating the first ctx, save the main ctx
	if (!g_usfstl_main_ctx) {
		USFSTL_ASSERT(!g_usfstl_current_ctx,
			      "unexpectedly have current context w/o main context");
		g_usfstl_main_ctx = usfstl_current_ctx();
	}

	USFSTL_ASSERT_EQ(getcontext(&ctx->ctx), 0, "%d");

	usfstl_ctx_common_init(ctx, name, fn, free, data);

	ctx->ctx.uc_stack.ss_size = 1024 * 128;
	ctx->ctx.uc_stack.ss_sp = calloc(1, ctx->ctx.uc_stack.ss_size);
	USFSTL_ASSERT(ctx->ctx.uc_stack.ss_sp);
	ctx->asan.bottom = ctx->ctx.uc_stack.ss_sp;
	ctx->asan.size = ctx->ctx.uc_stack.ss_size;

	makecontext(&ctx->ctx, (void *)_usfstl_context_fn, 1, ctx);

	return ctx;
}

static struct usfstl_ctx *usfstl_create_main_ctx(void)
{
	struct usfstl_ctx *ctx = calloc(1, sizeof(*ctx));

	ctx->common.name = "main";

	return ctx;
}

struct usfstl_ctx *usfstl_current_ctx(void)
{
	if (!g_usfstl_current_ctx) {
		if (!g_usfstl_main_ctx)
			g_usfstl_main_ctx = usfstl_create_main_ctx();
		g_usfstl_current_ctx = g_usfstl_main_ctx;
	}

	return g_usfstl_current_ctx;
}

bool usfstl_ctx_is_main(void)
{
	return !g_usfstl_current_ctx || g_usfstl_main_ctx == g_usfstl_current_ctx;
}

struct usfstl_ctx *usfstl_main_ctx(void)
{
	return g_usfstl_main_ctx;
}

void usfstl_ctx_abort_test(void)
{
	// if we're in the main ctx, finish aborting directly
	if (usfstl_ctx_is_main())
		usfstl_complete_abort();
	// otherwise, switch to the main ctx and finish there
	usfstl_switch_ctx(g_usfstl_main_ctx);
}

void usfstl_end_ctx(struct usfstl_ctx *ctx)
{
	USFSTL_ASSERT_CMP(ctx, !=, g_usfstl_current_ctx,
			  CTX_ASSERT_STR, CTX_ASSERT_VAL);

	/* switch to the ctx to use usfstl_end_self() for ASAN */
	g_usfstl_destroy_caller = usfstl_current_ctx();
	usfstl_switch_ctx(ctx);
}

static void _usfstl_switch_ctx(struct usfstl_ctx *ctx, bool terminate)
{
	struct usfstl_ctx *prev = usfstl_current_ctx();
	void *prev_stack = NULL, **prev_stack_ptr = &prev_stack;

	if (terminate) {
		g_usfstl_destroy_ctx = prev;
		prev_stack_ptr = NULL;
	}

	__sanitizer_start_switch_fiber(prev_stack_ptr,
				       ctx->asan.bottom,
				       ctx->asan.size);

	swapcontext(&prev->ctx, &ctx->ctx);

	// restore current ctx pointer and stack start before continuing to run
	_usfstl_complete_ctx_switch(prev, prev_stack);

	// if g_usfstl_test_aborted is set we're the main ctx asked to abort
	if (g_usfstl_test_aborted)
		usfstl_complete_abort();
}

void usfstl_end_self(struct usfstl_ctx *next)
{
	_usfstl_switch_ctx(next, true);
}

void usfstl_switch_ctx(struct usfstl_ctx *ctx)
{
	_usfstl_switch_ctx(ctx, false);
}

void usfstl_ctx_free_main(void)
{
	free(g_usfstl_main_ctx);

	g_usfstl_main_ctx = NULL;
	g_usfstl_destroy_ctx = NULL;
	g_usfstl_current_ctx = NULL;
	g_usfstl_destroy_caller = NULL;
}
