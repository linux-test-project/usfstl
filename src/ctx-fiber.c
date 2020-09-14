/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <assert.h>
#include <usfstl/test.h>
#include <usfstl/ctx.h>
#include <stdbool.h>
#ifdef _WIN32
#include <windows.h>
#else
typedef void VOID;
typedef void *LPVOID;
static inline LPVOID CreateFiber(unsigned int stacksize,
				 VOID (*fn)(LPVOID arg),
				 LPVOID arg)
{
	assert(0);
}

static inline LPVOID ConvertThreadToFiber(LPVOID data)
{
	assert(0);
}

static inline VOID DeleteFiber(LPVOID fiber)
{
	assert(0);
}

static inline VOID SwitchToFiber(LPVOID fiber)
{
	assert(0);
}
#define __stdcall
#endif
#include "internal.h"

struct usfstl_ctx *g_usfstl_destroy_ctx;
/*
 * The __thread annotation isn't needed for usfstl itself, but if called from
 * somebody who creates threads on the fly for running tests (like fullSim)
 * it was the only way I could figure how how to know when to call
 * CreateFiberFromThread() as even GetCurrentFiber() doesn't return NULL if
 * the thread hasn't called it, and GetFiberData() just crashes.
 */
__thread struct usfstl_ctx *g_usfstl_current_ctx;
__thread struct usfstl_ctx *g_usfstl_main_ctx;

struct usfstl_ctx {
	struct usfstl_ctx_common common;

	void *fiber;
};

static VOID __stdcall _usfstl_context_fn(LPVOID arg)
{
	struct usfstl_ctx *ctx = arg;

	g_usfstl_current_ctx = ctx;

	usfstl_set_stack_start(&ctx);
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

	/* ensure we are a fiber now and save the main ctx */
	if (!g_usfstl_main_ctx) {
		assert(!g_usfstl_current_ctx);
		g_usfstl_main_ctx = usfstl_current_ctx();
	}

	ctx->fiber = CreateFiber(1024 * 128, _usfstl_context_fn, ctx);
	assert(ctx->fiber);
	usfstl_ctx_common_init(ctx, name, fn, free, data);

	return ctx;
}

static struct usfstl_ctx *usfstl_create_main_ctx(void)
{
	struct usfstl_ctx *ctx;

	ctx = calloc(1, sizeof(*ctx));
	ctx->common.name = "main";
	ctx->fiber = ConvertThreadToFiber(g_usfstl_main_ctx);
	assert(ctx->fiber);

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

void usfstl_free_ctx_specific(struct usfstl_ctx *ctx)
{
	free(ctx);
}

void usfstl_end_ctx(struct usfstl_ctx *ctx)
{
	DeleteFiber(ctx->fiber);
	usfstl_free_ctx(ctx);
}

static void _usfstl_switch_ctx(struct usfstl_ctx *ctx, bool terminate)
{
	struct usfstl_ctx *prev = usfstl_current_ctx();

	if (terminate)
		g_usfstl_destroy_ctx = prev;

	SwitchToFiber(ctx->fiber);
	g_usfstl_current_ctx = prev;

	if (g_usfstl_destroy_ctx) {
		usfstl_end_ctx(g_usfstl_destroy_ctx);
		g_usfstl_destroy_ctx = NULL;
	}

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
}
