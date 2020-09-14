/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __SUPP_LIB_H
#define __SUPP_LIB_H

// get the original lib.h
#include_next <lib.h>

/*
 * override print()
 *
 * We didn't really need to do this, we could
 * have just provided an implementation of
 * print(), but this also shows how we can use
 * include order and #include_next tricks.
 */
#define print supp_print
void print(int x);

extern int g_printed_value;

#endif // __SUPP_LIB_H
