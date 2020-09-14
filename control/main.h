/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _MAIN_H_
#define _MAIN_H_
#include <usfstl/sched.h>

extern struct usfstl_scheduler scheduler;

void net_init(void);
void net_exit(void);

#endif
