/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _USFSTL_TASK_H_
#define _USFSTL_TASK_H_
#include <stdint.h>
#include "sched.h"
#include "list.h"

/*
 * Cooperative multi-threading implementation for usfstl.
 *
 * This builds on top of the usfstlsched.h and usfstlctx.h APIs to build a
 * cooperative (non-preemptible) multi-threading implementation. A task
 * here is a combination of an usfstl_ctx (i.e. a runnable context that has
 * its own stack) with a job that determines when it is runnable, and
 * functions to manage all the necessary APIs like sleep(), yield() etc.
 *
 * The scheduler's group concept is applicable to tasks as well, and
 * this can be used to implement different groups of tasks, for example:
 *
 *  - The main task always has group 0, this cannot be changed, you would
 *    typically use it to run the test code. When the test function returns
 *    to the framework the test completes successfully.
 *  - Another group can be used to implement simulation code, e.g. if you
 *    need to simulate an external instance that'd normally run on another
 *    CPU. For an embedded system this might be a host that controls it,
 *    external interfaces, etc.
 *  - A group for in-simulation tasks, i.e. tasks that run inside the
 *    main simulated (tested) system. Having a separate group for them is
 *    useful as it allows blocking that group when e.g. only a check for
 *    interrupts is desired, not a preemption point.
 *  - Another group (or set of groups) for simulated interrupt jobs, so
 *    that APIs to enable/disable interrupts can be simulated easily.
 *
 * Additionally, basic semaphore APIs are supported.
 *
 * Note that currently, only the single g_usfstl_task_scheduler is supported,
 * but you MUST initialize it (call usfstl_sched_init()) yourself, possibly
 * after setting the necessary callback functions in it.
 */

extern struct usfstl_scheduler g_usfstl_task_scheduler;

struct usfstl_task;

/**
 * usfstl_task_main - get main task
 *
 * Returns: the pointer to the main task.
 *
 * NOTE: Just like usfstl_main_task() (which this uses) it will return
 *       %NULL before any tasks have been created.
 */
struct usfstl_task *usfstl_task_main(void);

/**
 * usfstl_task_create - create a new task
 *
 * @name: name for the task
 * @group: scheduler group for this new task
 * @fn: function to call when the task runs
 * @free: function to call when the task is freed, whether by
 *	usfstl_task_end_self(), usfstl_task_end() or the test
 *	having ended/being aborted
 * @data: data passed to the task function
 *
 * Creates a new task. Note that new tasks are created in suspended
 * state, so you need to usfstl_task_resume() the new task to actually
 * make it runnable.
 */
struct usfstl_task *usfstl_task_create(const char *name, uint8_t group,
				       void (*fn)(struct usfstl_task *task,
						  void *data),
				       void (*free)(struct usfstl_task *task,
						    void *data),
				       void *data);

/**
 * usfstl_task_current - retrieve current task
 *
 * Retrieve the pointer to the task calling the function.
 * Note that this will return %NULL if called outside of
 * the context of a task, e.g. while waiting for scheduling
 * or similar.
 */
struct usfstl_task *usfstl_task_current(void);

/**
 * usfstl_task_set_priority - set task priority
 * @task: task to set priority on
 * @prio: new priority value - higher value means higher priority
 *
 * Set the task priority. Note that you can only call this on a
 * task that's not scheduled to run. Call it only on a suspended
 * task, or on the current task.
 */
void usfstl_task_set_priority(struct usfstl_task *task, uint32_t prio);

/**
 * usfstl_task_get_priority - get task priority
 * @task: task to set priority on
 *
 * Retrieve the task's priority.
 */
uint32_t usfstl_task_get_priority(const struct usfstl_task *task);

/**
 * usfstl_task_set_group - set task's job group
 * @task: task to set the group on
 * @group: new group value
 *
 * Modify the task's group, for example to let one task be
 * schedulable while others are not (for critical sections or
 * similar)
 */
void usfstl_task_set_group(struct usfstl_task *task, uint8_t group);

/**
 * usfstl_task_get_group - get task's job group
 * @task: task to get the group from
 */
uint8_t usfstl_task_get_group(const struct usfstl_task *task);

/**
 * usfstl_task_get_name - retrieve task name
 * @task: task to retrieve name for
 *
 * Returns: the task name given on creation.
 */
const char *usfstl_task_get_name(const struct usfstl_task *task);

/**
 * usfstl_task_get_data - retrieve task data
 * @task: task to retrieve data for
 *
 * Retrieves the data that was associated with the task when it
 * was created; if called for the main task/task (that created
 * all others) it is %NULL by default, unless it was changed by a
 * call to usfstl_task_set_data().
 */
void *usfstl_task_get_data(const struct usfstl_task *task);

/**
 * usfstl_task_set_data - set task data
 * @task: task to set data on
 * @data: data pointer to use
 *
 * Set the data that associated with the task.
 */
void usfstl_task_set_data(struct usfstl_task *task, void *data);

/**
 * usfstl_task_end_self - end the calling task
 *
 * End the task calling the function and switch to another runnable
 * task. This function will not return, asserting if there's no way
 * to do the requested action, i.e. there's nothing else runnable in
 * the system.
 * This is equivalent to returning from the task function, but can
 * be done at any point.
 *
 * Note that this also frees the task pointer.
 */
void usfstl_task_end_self(void);

/**
 * usfstl_task_end - end a task
 * @task: task to end
 *
 * End (and free) the given task.
 */
void usfstl_task_end(struct usfstl_task *task);

/**
 * usfstl_task_sleep - sleep
 * @delay: time to sleep for
 *
 * Sleep for the given time, which of course may end up being
 * longer in certain circumstances (e.g. a higher priority
 * task simulates using a lot of CPU time.)
 */
void usfstl_task_sleep(uint64_t delay);

/**
 * usfstl_task_yield - yield execution
 *
 * Yield to any higher-priority tasks, if any.
 */
static inline void usfstl_task_yield(void)
{
	usfstl_task_sleep(0);
}

/**
 * usfstl_task_suspend - suspend the calling task
 *
 * Suspend the calling task. It can only run again if resumed
 * with a call to usfstl_task_resume().
 */
void usfstl_task_suspend(void);

/**
 * usfstl_task_resume - resume a task
 * @task: task to resume
 *
 * Resume the given task. Since new tasks are created in
 * suspended state (e.g. to allow setting priority, etc.) this
 * must be called on each newly created task.
 */
void usfstl_task_resume(struct usfstl_task *task);

/**
 * usfstl_task_from_job - return task from job
 * @job: scheduler job to return the task for
 *
 * Returns: The task if the job is actually for one,
 *	    %NULL otherwise.
 */
struct usfstl_task *usfstl_task_from_job(struct usfstl_job *job);

/**
 * usfstl_job_from_task - return job from task
 * @task: task to return the scheduler job for
 *
 * Returns: The job for the given task.
 */
struct usfstl_job *usfstl_job_from_task(struct usfstl_task *task);

/**
 * g_usfstl_task_leave - task leave callback
 *
 * Use this for debugging, called whenever we switch away from
 * a task and enter the scheduler. Note that usfstl_task_current()
 * may still return the old task since the scheduler runs in
 * the same context, however, this should only be visible in the
 * scheduler's time update callback.
 */
extern void (*g_usfstl_task_leave)(void);

/**
 * g_usfstl_task_enter - task (re)enter callback
 *
 * Use this also for debugging, called whenever a task is entered
 * again (or for the first time) after scheduling.
 *
 * Note that leave/enter are also called if the task just sleeps
 * and then runs again itself.
 */
extern void (*g_usfstl_task_enter)(void);

/**
 * struct usfstl_sem - usfstl semaphore
 * @name: name given to the semaphore, will be copied to the
 *	scheduler job if a task is waiting for this with
 *	a timeout
 */
struct usfstl_sem {
	const char *name;
/* private: */
	struct usfstl_list waiters;
	uint32_t ctr;
};

/**
 * usfstl_sem_wait - decrement the given semaphore
 * @sem: semaphore to decrement
 */
void usfstl_sem_wait(struct usfstl_sem *sem);

/**
 * usfstl_sem_trywait - try to decrement the given semaphore
 * @sem: semaphore to decrement
 *
 * This function doesn't block.
 *
 * Returns: %true if the semaphore could be decremented, %false otherwise
 */
bool usfstl_sem_trywait(struct usfstl_sem *sem);

/**
 * usfstl_sem_timedwait - decrement the given semaphore with timeout
 * @sem: semaphore to decrement
 * @timeout: how long to wait at most (relative time)
 *
 * Returns: %true if the semaphore could be decremented, %false otherwise
 */
bool usfstl_sem_timedwait(struct usfstl_sem *sem, uint64_t timeout);

/**
 * usfstl_sem_post - increment the given semaphore
 * @sem: semaphore to increment
 */
void usfstl_sem_post(struct usfstl_sem *sem);

static inline bool usfstl_sem_has_waiters(const struct usfstl_sem *sem)
{
	return sem->waiters.list.next && !usfstl_list_empty(&sem->waiters);
}

#endif /* _USFSTL_TASK_H_ */
