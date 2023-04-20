/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <lib.h>

int g_sum;

int dummy1(void)
{
#ifdef CONFIG_A
	return 42;
#elif CONFIG_B
	return 43;
#else
#error "don't know this config"
#endif
}

int dummy2(int x, int y, int z, int zz)
{
	g_sum += x + y + z + zz;
	return dummy1() + x + y + z + zz;
}

void dummy3(int x)
{
	/*
	 * This here illustrates how support code works,
	 * the print function only exists in the main.c
	 * and in the support code.
	 */
	print(dummy2(x, 0, 0, 0));
}
