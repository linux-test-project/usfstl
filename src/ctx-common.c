/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <usfstl/test.h>
#include <usfstl/ctx.h>
#include "internal.h"

static struct usfstl_ctx_common *g_usfstl_ctxs;

void usfstl_ctx_common_init(struct usfstl_ctx *ctx, const char *name,
			    void (*fn)(struct usfstl_ctx *, void *),
			    void (*free)(struct usfstl_ctx *, void *),
			    void *data)
{
	struct usfstl_ctx_common *common = (void *)ctx;

	common->name = name;
	common->fn = fn;
	common->free = free;
	common->data = data;

	common->next = g_usfstl_ctxs;
	g_usfstl_ctxs = common;
}

void usfstl_free_ctx(struct usfstl_ctx *ctx)
{
	struct usfstl_ctx_common *common = (void *)ctx;
	struct usfstl_ctx_common *tmp, **pnext;
	bool found = false;

	USFSTL_ASSERT_CMP(usfstl_current_ctx(), !=, ctx,
			  CTX_ASSERT_STR, CTX_ASSERT_VAL);

	for (pnext = &g_usfstl_ctxs, tmp = g_usfstl_ctxs;
	     tmp;
	     pnext = &tmp->next, tmp = tmp->next) {
		if (tmp == common) {
			found = true;
			*pnext = common->next;
			break;
		}
	}

	USFSTL_ASSERT(found, "couldn't find context to free??");

	if (common->free)
		common->free(ctx, common->data);
	usfstl_free_ctx_specific(ctx);
}

static struct usfstl_ctx_common *usfstl_current_ctx_common(void)
{
	return (void *)usfstl_current_ctx();
}

void *usfstl_get_stack_start(void)
{
	return usfstl_current_ctx_common()->stack_start;
}

void usfstl_set_stack_start(void *p)
{
	usfstl_current_ctx_common()->stack_start = p;
}

const char *usfstl_ctx_get_name(struct usfstl_ctx *ctx)
{
	struct usfstl_ctx_common *common = (void *)ctx;

	return common->name;
}

void *usfstl_ctx_get_data(struct usfstl_ctx *ctx)
{
	struct usfstl_ctx_common *common = (void *)ctx;

	return common->data;
}

void usfstl_ctx_set_data(struct usfstl_ctx *ctx, void *data)
{
	struct usfstl_ctx_common *common = (void *)ctx;

	common->data = data;
}

void usfstl_ctx_set_name(struct usfstl_ctx *ctx, const char *name)
{
	struct usfstl_ctx_common *common = (void *)ctx;

	common->name = name;
}

void usfstl_ctx_cleanup(void)
{
	USFSTL_ASSERT(usfstl_ctx_is_main(),
		      "called context cleanup outside main context");
	while (g_usfstl_ctxs)
		usfstl_free_ctx((void *)g_usfstl_ctxs);
	usfstl_ctx_free_main();
}
