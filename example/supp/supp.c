/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "lib.h"

int g_printed_value;

void print(int x)
{
	g_printed_value = x;
}
