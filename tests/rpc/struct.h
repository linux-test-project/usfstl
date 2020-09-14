/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __STRUCT_H
#define __STRUCT_H
#include <stdint.h>

struct foo {
	int32_t bar;
};

struct log {
	char msg[0];
};

#endif // __STRUCT_H
