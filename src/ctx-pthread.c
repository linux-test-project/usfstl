/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <usfstl/ctx.h>
#include <usfstl/test.h>
#include "internal.h"

#ifdef _WIN32
static inline int _pthread_setname_np(pthread_t threadId, const char *name)
{
	return 0;
}
#define pthread_setname_np _pthread_setname_np
#endif

static __thread struct usfstl_ctx *g_usfstl_current_ctx;

static struct usfstl_ctx *g_usfstl_main_ctx;

struct usfstl_ctx {
	/* must be first */
	struct usfstl_ctx_common common;

	sem_t sem;
	pthread_t thread;
};

static void *_usfstl_thread_fn(void *_ctx)
{
	struct usfstl_ctx *ctx = _ctx;

	g_usfstl_current_ctx = ctx;

	// wait until we're scheduled the first time
	USFSTL_ASSERT(sem_wait(&ctx->sem) == 0,
		      "sem_wait() failed, errno %d", errno);

	usfstl_set_stack_start(&ctx);
	ctx->common.fn(ctx, ctx->common.data);

	usfstl_end_self(g_usfstl_main_ctx);

	return NULL;
}

struct usfstl_ctx *
usfstl_ctx_create(const char *name,
		  void (*fn)(struct usfstl_ctx *ctx, void *data),
		  void (*free)(struct usfstl_ctx *ctx, void *data),
		  void *data)
{
	struct usfstl_ctx *ctx = calloc(1, sizeof(*ctx));

	// save main ctx on creating any new ones
	if (!g_usfstl_main_ctx) {
		USFSTL_ASSERT(!g_usfstl_current_ctx);
		g_usfstl_main_ctx = usfstl_current_ctx();
	}

	usfstl_ctx_common_init(ctx, name, fn, free, data);
	USFSTL_ASSERT(sem_init(&ctx->sem, 0, 0) == 0,
		      "sem_init() failed, errno = %d", errno);

	USFSTL_ASSERT_EQ(pthread_create(&ctx->thread, NULL, _usfstl_thread_fn, ctx),
			 0, "%d");

	pthread_setname_np(ctx->thread, name);

	return ctx;
}

static struct usfstl_ctx *usfstl_create_main_ctx(void)
{
	struct usfstl_ctx *ctx = calloc(1, sizeof(*ctx));

	ctx->common.name = "main";
	ctx->thread = pthread_self();
	USFSTL_ASSERT(sem_init(&ctx->sem, 0, 0) == 0,
		      "sem_init() failed, errno = %d", errno);

	return ctx;
}

struct usfstl_ctx *usfstl_current_ctx(void)
{
	if (!g_usfstl_current_ctx) {
		if (!g_usfstl_main_ctx)
			g_usfstl_main_ctx = usfstl_create_main_ctx();
		/* should not get called from another thread */
		USFSTL_ASSERT(g_usfstl_main_ctx->thread == pthread_self(),
			      "calling usfstl_current_ctx() to initialize non-main thread??");
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
	int ret = pthread_cancel(ctx->thread);

	USFSTL_ASSERT(ret == 0 || ret == ESRCH,
		      "unexpected return from pthread_cancel(): %d", ret);

	ret = pthread_join(ctx->thread, NULL);
	USFSTL_ASSERT(ret == 0 || ret == ESRCH,
		      "unexpected return from pthread_join(): %d", ret);

	USFSTL_ASSERT(sem_destroy(&ctx->sem) == 0,
		      "sem_destroy() failed, errno = %d", errno);
	free(ctx);
}

void usfstl_end_ctx(struct usfstl_ctx *ctx)
{
	usfstl_free_ctx(ctx);
}

void usfstl_end_self(struct usfstl_ctx *next)
{
	USFSTL_ASSERT(sem_post(&next->sem) == 0,
		      "sem_post() failed, errno = %d", errno);
	pthread_exit(NULL);
}

void usfstl_switch_ctx(struct usfstl_ctx *ctx)
{
	USFSTL_ASSERT(sem_post(&ctx->sem) == 0,
		      "sem_post() failed, errno = %d", errno);
	USFSTL_ASSERT(sem_wait(&g_usfstl_current_ctx->sem) == 0,
		      "sem_wait() failed, errno = %d", errno);

	// if g_usfstl_test_aborted is set we're the main ctx asked to abort
	if (g_usfstl_test_aborted)
		usfstl_complete_abort();
}

void usfstl_ctx_free_main(void)
{
	if (g_usfstl_main_ctx) {
		USFSTL_ASSERT(sem_destroy(&g_usfstl_main_ctx->sem) == 0,
			      "sem_destroy() failed, errno = %d", errno);
		free(g_usfstl_main_ctx);
	}

	g_usfstl_main_ctx = NULL;
	g_usfstl_current_ctx = NULL;
}
