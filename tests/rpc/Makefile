#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
CFLAGS += -I../../include/ -g -Werror -Wall -Wextra -Wno-unused-parameter

all: client server local

rpc%o:	../../src/rpc%c
	$(CC) -c -o $@ $^ $(CFLAGS)
loop.o:	../../src/loop.c
	$(CC) -c -o $@ $^ $(CFLAGS)

client:	rpc.o rpc-rpc.o rpc-posix.o caller.o callee.o impl.o client.o calls.o loop.o
	$(CC) -o client $^
server: rpc.o rpc-rpc.o rpc-posix.o caller.o callee.o impl.o server.o         loop.o
	$(CC) -o server $^
local:  rpc.o rpc-rpc.o rpc-posix.o caller.o callee.o impl.o local.o  calls.o loop.o
	$(CC) -o local $^

test: all
	./client
	./local

clean:
	@rm -f *~ direct client server local *.o
