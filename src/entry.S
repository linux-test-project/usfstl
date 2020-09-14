//
// Copyright (C) 2020 Intel Corporation
//
// SPDX-License-Identifier: BSD-3-Clause
//
#if defined(__x86_64__) || defined(__i386__)
#include "entry-x86.s"
#else
#error "Don't know how to run on this architecture"
#endif
