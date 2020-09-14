/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include "lib.h"

void print(int x)
{
	printf("%d\n", x);
}

int main(int argc, char **argv)
{
	printf("%d\n", dummy1());
	printf("%d\n", dummy2(100));
	dummy2(100);
	dummy3(1000); // calls dummy2() internally

	printf("sum = %d\n", g_sum);
}
