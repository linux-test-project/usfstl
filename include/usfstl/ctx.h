/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * usfstl's simple (execution) context abstraction
 */
#ifndef _USFSTL_CTX_H_
#define _USFSTL_CTX_H_
#include <stdbool.h>

struct usfstl_ctx;

/**
 * usfstl_ctx_add - add a ctx to the scheduler
 * @name: ctx name
 * @fn: function to run
 * @free: function to run on freeing the ctx, be it due to
 *	ending it or due to test complete/abort
 * @data: data to pass to the function
 */
struct usfstl_ctx *usfstl_ctx_create(const char *name,
				     void (*fn)(struct usfstl_ctx *ctx,
						void *data),
				     void (*free)(struct usfstl_ctx *ctx,
						  void *data),
				     void *data);

/**
 * usfstl_current_ctx - return current ctx
 * Returns: The current ctx.
 */
struct usfstl_ctx *usfstl_current_ctx(void);

/**
 * usfstl_ctx_get_name - retrieve ctx name
 * @ctx: ctx to retrieve name from
 *
 * Returns: the name of the ctx given on creation
 */
const char *usfstl_ctx_get_name(struct usfstl_ctx *ctx);

/**
 * usfstl_ctx_is_main - check if current ctx is main ctx
 *
 * Returns %true if the current ctx is the main (test) ctx,
 * %false otherwise.
 */
bool usfstl_ctx_is_main(void);

/**
 * usfstl_main_ctx - return main ctx
 *
 * Returns: the ctx pointer for the main ctx.
 * NOTE: this will return %NULL if called before any
 *       other ctxs are created by usfstl_ctx_create()
 */
struct usfstl_ctx *usfstl_main_ctx(void);

/**
 * usfstl_end_ctx - end the given ctx
 * @ctx: the ctx to end
 */
void usfstl_end_ctx(struct usfstl_ctx *ctx);

/**
 * usfstl_end_self - end self and switch to given ctx
 * @next: ctx to switch to
 */
void usfstl_end_self(struct usfstl_ctx *next);

/**
 * usfstl_switch_ctx - switch to given ctx
 * @ctx: ctx to switch to, or %NULL
 */
void usfstl_switch_ctx(struct usfstl_ctx *ctx);

/**
 * usfstl_ctx_get_data - get ctx data
 * @ctx: ctx to return data for
 *
 * Returns: the data pointer originally given to
 *	    the ctx, or %NULL for the initial ctx
 *	    (unless usfstl_ctx_set_data() was called)
 */
void *usfstl_ctx_get_data(struct usfstl_ctx *ctx);

/**
 * usfstl_ctx_set_data - set ctx data
 * @ctx: ctx to set data on
 * @data: data to set
 */
void usfstl_ctx_set_data(struct usfstl_ctx *ctx, void *data);

/**
 * usfstl_ctx_set_name - set ctx name
 * @ctx: ctx to set name on
 * @name: name to set
 */
void usfstl_ctx_set_name(struct usfstl_ctx *ctx, const char *name);

#endif // _USFSTL_CTX_H_
