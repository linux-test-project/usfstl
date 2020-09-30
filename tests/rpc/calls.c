/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include "rpc.h"

void calls(void)
{
	struct foo in = { .bar = 100 };
	struct {
		struct log hdr;
		char message[20];
	} __attribute__((packed)) log = {
		.message = "hello there!",
	}, name = {
		.message = "Jane Doe",
	}, ret;

	printf("%d\n", callme1(100));
	printf("%d\n", callme2(&in));
	callme3(100);
	callme4(&in);
	hello(&name.hdr, sizeof(name), &ret.hdr, sizeof(ret));
	hello(&name.hdr, sizeof(name.hdr), &ret.hdr, sizeof(ret));
	printf("%.*s\n", (int)(sizeof(ret) - sizeof(ret.hdr)), ret.hdr.msg);

	rpclog(&log.hdr, sizeof(log));

	numformat(123901824, &ret.hdr, sizeof(ret));
	printf("%.*s\n", (int)(sizeof(ret) - sizeof(ret.hdr)), ret.hdr.msg);
	numformatp(&in, &ret.hdr, sizeof(ret));
	printf("%.*s\n", (int)(sizeof(ret) - sizeof(ret.hdr)), ret.hdr.msg);

	// async
	callme5(101);
	in.bar = 101;
	callme6(&in);
	callme7(&ret.hdr, sizeof(ret));
}
