/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <usfstl/test.h>
#include <usfstl/log.h>
#include <usfstl/rpc.h> // it may include winsock and is in internal.h
#include <stdarg.h>
#include <stdlib.h>
#include <windows.h>
#include "internal.h"

static HANDLE g_usfstl_timer;
static uint64_t g_usfstl_timeout_expiry;

static uint64_t get_used_cpu_time_ms(void)
{
	FILETIME creation_time, exit_time, kernel_time, user_time;
	uint64_t t;

	GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time);
	t = user_time.dwLowDateTime + ((uint64_t)user_time.dwHighDateTime << 32);
	// times in FILETIME are in 100-nanosecond intervals
	t /= 10; // microseconds
	t /= 1000; // milliseconds
	return t;
}

static void CALLBACK usfstl_watchdog_expired(void *data, BOOLEAN fired)
{
	HANDLE mainthread = data;
	CONTEXT ctx = {
		.ContextFlags = CONTEXT_FULL,
	};

	if (g_usfstl_timeout_expiry >= get_used_cpu_time_ms())
		return;

	// make the main thread execute usfstl_mainthread_watchdog_expired,
	// we don't really care what happens to the stack or anything as
	// we'll just do a longjmp() there, which we can't do here in the
	// timer helper thread
	SuspendThread(mainthread);
	GetThreadContext(mainthread, &ctx);
#ifdef __x86_64__
	ctx.Rcx = ctx.Rip;
	ctx.Rip = (unsigned long)usfstl_out_of_time;
#else
	ctx.Ecx = ctx.Eip;
	ctx.Eip = (DWORD)usfstl_out_of_time;
#endif
	SetThreadContext(mainthread, &ctx);
	ResumeThread(mainthread);
}

void usfstl_watchdog_start(unsigned int timeout_ms)
{
	HANDLE mainthread;

	DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
			&mainthread, 0, TRUE, DUPLICATE_SAME_ACCESS);

	usfstl_watchdog_reset(timeout_ms);

	// run the watchdog in intervals of 100ms - could be tuned but should
	// be good enough since it's just intended to catch run-away tests
	if (!CreateTimerQueueTimer(&g_usfstl_timer, NULL, usfstl_watchdog_expired,
				   mainthread, 100, 100, WT_EXECUTEINPERSISTENTTHREAD)) {
		usfstl_printf("CreateTimerQueueTimer failed!\n");
		abort();
	}
}

void usfstl_watchdog_reset(unsigned int timeout_ms)
{
	g_usfstl_timeout_expiry = get_used_cpu_time_ms() + timeout_ms;
}

void usfstl_watchdog_stop(void)
{
	DeleteTimerQueueTimer(NULL, g_usfstl_timer, NULL);
}
