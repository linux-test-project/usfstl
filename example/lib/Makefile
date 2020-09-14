#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
CONFIG ?= A
OUTDIR ?= .

test: $(OUTDIR)/main.o $(OUTDIR)/lib.a
	cc -o $@ $^

# Note the -I. here, so we can use <lib.h>
override CFLAGS += -DCONFIG_${CONFIG} -I.

$(OUTDIR)/lib.a: $(OUTDIR)/code.o
	ar cr $@ $(foreach obj,$<,$(abspath ${obj}))
$(OUTDIR)/code.o: code.c lib.h
	cc -c -o $@ $< $(CFLAGS)

main.o:	main.c lib.h
	cc -c -o $@ $< $(CFLAGS)

lib: $(OUTDIR)/lib.a
clean:
	rm -f code.o lib.a test
