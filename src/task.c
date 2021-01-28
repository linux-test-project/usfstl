/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <usfstl/test.h>
#include <usfstl/task.h>
#include <usfstl/sched.h>
#include <usfstl/ctx.h>
#include "internal.h"

static struct usfstl_task *g_usfstl_task_to_end;
// initialize to true, since the main task is running at the start
static bool g_usfstl_task_running = true;

USFSTL_SCHEDULER(g_usfstl_task_scheduler);

struct usfstl_scheduler *USFSTL_NORESTORE_VAR(g_usfstl_top_scheduler) =
	&g_usfstl_task_scheduler;

void (*g_usfstl_task_leave)(void);
void (*g_usfstl_task_enter)(void);


struct usfstl_task {
	struct usfstl_ctx *ctx;
	struct usfstl_job job;
	void (*fn)(struct usfstl_task *, void *);
	void (*free)(struct usfstl_task *, void *);
	void *data;

	struct usfstl_list_entry sem_entry;
};

static void usfstl_task_next(void)
{
	struct usfstl_job *ran;

	if (g_usfstl_task_leave)
		g_usfstl_task_leave();
	g_usfstl_task_running = false;

	do {
		ran = usfstl_sched_next(&g_usfstl_task_scheduler);
	} while (!usfstl_task_from_job(ran));

	g_usfstl_task_running = true;
	if (g_usfstl_task_enter)
		g_usfstl_task_enter();
}

static void usfstl_task_ctx_fn(struct usfstl_ctx *ctx, void *data)
{
	struct usfstl_task *task = data;

	g_usfstl_task_running = true;
	if (g_usfstl_task_enter)
		g_usfstl_task_enter();

	task->fn(task, task->data);

	usfstl_task_end_self();
}

static void usfstl_task_ctx_free(struct usfstl_ctx *ctx, void *data)
{
	struct usfstl_task *task = data;

	if (task->free)
		task->free(task, task->data);
	free(task);
}

static void usfstl_task_job_fn(struct usfstl_job *job)
{
	struct usfstl_task *task = job->data;

	/*
	 * NOTE - this is the *job* function, it still runs in the *context*
	 *	  of task that called usfstl_sched_next()! This is still the
	 *	  'previous' task (in some sense), but usfstl_task_current()
	 *	  will be returning NULL until the next task starts again.
	 *
	 *	  The argument of this function is the job for the task that
	 *	  we now need to switch to, which we typically do below by
	 *	  calling usfstl_switch_ctx(task->ctx), unless we need to end
	 *	  the previous/still-current task, in that case we need to
	 *	  call usfstl_end_self() instead, giving the new task as the
	 *	  argument to switch to.
	 */

	if (g_usfstl_task_to_end) {
		/*
		 * After setting usfstl_task_end_self, we immediately call
		 * usfstl_sched_next() in the context of the task/ctx that
		 * was calling usfstl_task_end_self().
		 *
		 * Thus, only two things can happen:
		 *  1) Some custom scheduler jobs are next, and the function
		 *     is called, but those cannot cause a ctx switch.
		 *  2) Another task job is next, so we get here.
		 *
		 * Therefore, we must still be in the context of the task that
		 * wanted to end itself, so finish this context by switching to
		 * the task we actually want to run.
		 *
		 * Note that this function never returns, the current context
		 * is freed (and the free function will free the task as well.)
		 *
		 * Note that we don't use usfstl_task_current() here as it will
		 * return NULL while we're in the scheduler, but we do want to
		 * ensure that we're in the context of the task that's ending
		 * itself, so compare that.
		 */
		USFSTL_ASSERT_EQ(g_usfstl_task_to_end->ctx, usfstl_current_ctx(),
				 CTX_ASSERT_STR, CTX_ASSERT_VAL);
		g_usfstl_task_to_end = NULL;
		usfstl_end_self(task->ctx);
	}

	/*
	 * Now we switch to task->ctx. Once we return from that (in this or any
	 * other context) we'll return to the caller of usfstl_sched_next() and
	 * set usfstl_task_current() to return a proper task again.
	 */
	usfstl_switch_ctx(task->ctx);
}

static struct usfstl_task *
usfstl_task_alloc(void (*fn)(struct usfstl_task *task, void *data),
		  void (*free)(struct usfstl_task *task, void *data),
		  void *data)
{
	struct usfstl_task *task;

	task = calloc(1, sizeof(*task));
	USFSTL_ASSERT(task);

	task->data = data;
	task->fn = fn;
	task->free = free;

	task->job.callback = usfstl_task_job_fn;
	task->job.data = task;

	return task;
}

static void usfstl_task_alloc_main(void)
{
	struct usfstl_ctx *ctx = usfstl_main_ctx();
	struct usfstl_task *task;

	task = usfstl_task_alloc(NULL, NULL, NULL);
	task->ctx = ctx;
	usfstl_ctx_set_data(ctx, task);
}

void usfstl_task_cleanup(void)
{
	free(usfstl_ctx_get_data(usfstl_main_ctx()));
}

struct usfstl_task *usfstl_task_main(void)
{
	struct usfstl_ctx *ctx = usfstl_main_ctx();

	if (!usfstl_ctx_get_data(ctx))
		usfstl_task_alloc_main();
	return usfstl_ctx_get_data(ctx);
}

struct usfstl_task *usfstl_task_create(const char *name, uint8_t group,
				       void (*fn)(struct usfstl_task *task,
						  void *data),
				       void (*free)(struct usfstl_task *task,
						    void *data),
				       void *data)
{
	struct usfstl_task *task = usfstl_task_alloc(fn, free, data);

	task->ctx = usfstl_ctx_create(name, usfstl_task_ctx_fn,
				      usfstl_task_ctx_free, task);
	task->job.group = group;

	return task;
}

struct usfstl_task *usfstl_task_current(void)
{
	struct usfstl_ctx *ctx;
	struct usfstl_task *task;

	if (!g_usfstl_task_running)
		return NULL;

	ctx = usfstl_current_ctx();
	task = usfstl_ctx_get_data(ctx);

	if (task)
		return task;

	USFSTL_ASSERT_EQ(ctx, usfstl_main_ctx(), CTX_ASSERT_STR, CTX_ASSERT_VAL);
	usfstl_task_alloc_main();

	return usfstl_ctx_get_data(ctx);
}

void usfstl_task_set_priority(struct usfstl_task *task, uint32_t prio)
{
	USFSTL_ASSERT(!usfstl_job_scheduled(&task->job),
		    "cannot change task '%s' priority to %d while scheduled",
		    usfstl_task_get_name(task), prio);

	task->job.priority = prio;
}

uint32_t usfstl_task_get_priority(const struct usfstl_task *task)
{
	return task->job.priority;
}

void usfstl_task_set_group(struct usfstl_task *task, uint8_t group)
{
	USFSTL_ASSERT(!usfstl_job_scheduled(&task->job),
		    "cannot change task '%s' group to %d while scheduled",
		    usfstl_task_get_name(task), group);
	USFSTL_ASSERT_CMP(group, <, 32, "%u");

	task->job.group = group;
}

uint8_t usfstl_task_get_group(const struct usfstl_task *task)
{
	return task->job.group;
}

const char *usfstl_task_get_name(const struct usfstl_task *task)
{
	return usfstl_ctx_get_name(task->ctx);
}

void *usfstl_task_get_data(const struct usfstl_task *task)
{
	return task->data;
}

void usfstl_task_set_data(struct usfstl_task *task, void *data)
{
	task->data = data;
}

void usfstl_task_end_self(void)
{
	struct usfstl_task *task = usfstl_task_current();

	USFSTL_ASSERT(task, "can only call usfstl_task_end_self() while in task");
	USFSTL_ASSERT(!usfstl_job_scheduled(&task->job),
		      "task '%s' cannot end itself while scheduled",
		      usfstl_task_get_name(task));

	/* store self to end when next task schedules */
	g_usfstl_task_to_end = task;

	/* and then schedule */
	usfstl_task_next();

	USFSTL_ASSERT(0);
}

void usfstl_task_end(struct usfstl_task *task)
{
	struct usfstl_ctx *ctx = task->ctx;

	USFSTL_ASSERT(!usfstl_job_scheduled(&task->job),
		      "task '%s' cannot be ended while scheduled",
		      usfstl_task_get_name(task));

	/*
	 * As opposed to usfstl_task_end_self(), this case is easy;
	 * just end the context and the free function will be
	 * called to also free the associated task data.
	 */
	usfstl_end_ctx(ctx);
}

void usfstl_task_sleep(uint64_t delay)
{
	struct usfstl_task *task = usfstl_task_current();
	char jobbuf[26] = {};

	USFSTL_ASSERT(task, "can only call usfstl_task_sleep() while in task");

	// this is valid on the stack since we don't return from
	// this function until the job actually happens
	if (delay == 0)
		strcpy(jobbuf, "yield");
	else if (delay <= 0xffffffff)
		snprintf(jobbuf, sizeof(jobbuf) - 1, "sleep %d",
			 (unsigned int)delay);
	else
		snprintf(jobbuf, sizeof(jobbuf) - 1, "sleep 0x%08x%08x",
			 (unsigned int)(delay >> 32), (unsigned int)delay);
	task->job.name = jobbuf;

	/* run again when desired */
	task->job.start = usfstl_sched_current_time(&g_usfstl_task_scheduler) +
			  delay;
	usfstl_sched_add_job(&g_usfstl_task_scheduler, &task->job);

	usfstl_task_next();
}

void usfstl_task_suspend(void)
{
	usfstl_task_next();
}

void usfstl_task_resume(struct usfstl_task *task)
{
	task->job.start = usfstl_sched_current_time(&g_usfstl_task_scheduler);
	task->job.name = "preempt";
	usfstl_sched_add_job(&g_usfstl_task_scheduler, &task->job);
}

struct usfstl_task *usfstl_task_from_job(struct usfstl_job *job)
{
	if (job->callback != usfstl_task_job_fn)
		return NULL;
	return job->data;
}

struct usfstl_job *usfstl_job_from_task(struct usfstl_task *task)
{
	return &task->job;
}

static void usfstl_sem_init_if_needed(struct usfstl_sem *sem)
{
	if (!sem->waiters.list.next)
		usfstl_list_init(&sem->waiters);
}

#define NO_TIMEOUT ((uint64_t)~0ULL)

bool usfstl_sem_timedwait(struct usfstl_sem *sem, uint64_t timeout)
{
	struct usfstl_task *task = usfstl_task_current();

	USFSTL_ASSERT(task, "can only call usfstl_sem_timedwait() while in task");

	usfstl_sem_init_if_needed(sem);

	if (!sem->_name[0]) {
		snprintf(sem->_name, sizeof(sem->_name) - 1, "!%s",
			 sem->name ?: "");
		sem->_name[sizeof(sem->_name) - 1] = 0;
	}

	if (!sem->ctr) {
		struct usfstl_task *tmp;

		task->job.name = sem->_name + 1;

		if (timeout != NO_TIMEOUT) {
			/* run again when desired */
			task->job.start =
				usfstl_sched_current_time(&g_usfstl_task_scheduler) +
				timeout;
			usfstl_sched_add_job(&g_usfstl_task_scheduler, &task->job);
		}

		/* keep priority-sorted list */
		usfstl_for_each_list_item(tmp, &sem->waiters, sem_entry) {
			if (usfstl_task_get_priority(tmp) >=
			    usfstl_task_get_priority(task))
				break;
		}
		if (!tmp)
			usfstl_list_append(&sem->waiters, &task->sem_entry);
		else
			usfstl_list_insert_before(&tmp->sem_entry,
						&task->sem_entry);

		usfstl_task_suspend();

		task->job.name = NULL;

		if (sem->ctr == 0) {
			USFSTL_ASSERT(task->sem_entry.next,
				      "task must be on semaphore list here");
			USFSTL_ASSERT_CMP(timeout, !=, NO_TIMEOUT, "%" PRIu64);
			usfstl_list_item_remove(&task->sem_entry);
			return false;
		}
	}

	sem->ctr--;
	return true;
}

void usfstl_sem_wait(struct usfstl_sem *sem)
{
	bool ret = usfstl_sem_timedwait(sem, NO_TIMEOUT);

	USFSTL_ASSERT(ret, "usfstl_sem_timedwait(NO_TIMEOUT) failed");
}

bool usfstl_sem_trywait(struct usfstl_sem *sem)
{
	usfstl_sem_init_if_needed(sem);

	if (sem->ctr > 0) {
		sem->ctr--;
		return true;
	}

	return false;
}

void usfstl_sem_post(struct usfstl_sem *sem)
{
	usfstl_sem_init_if_needed(sem);

	sem->ctr++;

	/* pick up the first waiter (priority list) */
	if (!usfstl_list_empty(&sem->waiters)) {
		struct usfstl_task *task;

		task = usfstl_list_first_item(&sem->waiters,
					      struct usfstl_task,
					      sem_entry);

		usfstl_sched_del_job(&task->job);

		task->job.start =
			usfstl_sched_current_time(&g_usfstl_task_scheduler);
		// Trick!
		// The name here points to the sem name prefixed with an
		// additional "!" so here we can "uncover" that to show
		// that the semaphore has been signaled.
		task->job.name -= 1;
		// re-add the job for the signal
		usfstl_sched_add_job(&g_usfstl_task_scheduler, &task->job);

		usfstl_list_item_remove(&task->sem_entry);
	}
}
