#!/bin/bash
#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
export AFL_NO_UI=1

N=$(( ( $(nproc) / 2 ) - 1 ))
AFL_ARGS="-t 2000+ -m 1000 -i data -o errors"
afl-fuzz $AFL_ARGS -M fuzzer000 -- "$@" --disable-wdt &
master=$!

for i in $(seq --format=%03.0f 1 $N) ; do
	afl-fuzz $AFL_ARGS -S fuzzer$i -- "$@" --disable-wdt &
done

wait $master || exit 2
