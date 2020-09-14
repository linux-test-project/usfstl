/*
 * Copyright (C) 2018 Intel Corporation
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <usfstl/test.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include "internal.h"

static struct sigaction g_usfstl_old_si;

static void usfstl_watchdog_handler(int mysignal, siginfo_t *si, void *arg)
{
	ucontext_t *context = (ucontext_t *)arg;
	unsigned int reg;

#ifdef __x86_64__
	reg = REG_RIP;
#else
	reg = REG_EIP;
#endif

	usfstl_out_of_time((void *)context->uc_mcontext.gregs[reg]);
}

void usfstl_watchdog_start(unsigned int timeout_ms)
{
	struct sigaction si;

	memset(&si, 0, sizeof(si));
	si.sa_handler = (void *)usfstl_watchdog_handler;
	si.sa_flags = SA_RESTART | SA_NODEFER | SA_SIGINFO;
	sigaction(SIGVTALRM, &si, &g_usfstl_old_si);

	usfstl_watchdog_reset(timeout_ms);
}

void usfstl_watchdog_reset(unsigned int timeout_ms)
{
	struct itimerval itv = {
		.it_value = {
			.tv_sec = timeout_ms / 1000,
			.tv_usec = 1000 * (timeout_ms % 1000),
		},
	};

	setitimer(ITIMER_VIRTUAL, &itv, NULL);
}

void usfstl_watchdog_stop(void)
{
	usfstl_watchdog_reset(0);
	sigaction(SIGUSR2, &g_usfstl_old_si, NULL);
}
