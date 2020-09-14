#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
CFLAGS += -I../include/ -g -Werror -Wall -Wextra -Wno-unused-parameter -Wno-format-zero-length
#CFLAGS += -fsanitize=address -fsanitize=undefined
CFLAGS += -D_GNU_SOURCE=1

all: controller

usfstl/%o: ../src/%c
	@mkdir -p usfstl
	$(CC) -c -o $@ $^ $(CFLAGS)

controller: main.o net.o usfstl/loop.o usfstl/uds.o usfstl/sched.o usfstl/vhost.o usfstl/opt.o
controller: usfstl/wallclock.o usfstl/schedctrl.o
	$(CC) -o $@ $^ #-lasan -lubsan

clean:
	@rm -rf *~ controller *.o usfstl
