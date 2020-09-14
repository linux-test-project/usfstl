#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
CFLAGS = -I.

override CFLAGS += -I.

$(OUTDIR)/supp.a: $(OUTDIR)/supp.o
	ar cr $@ $(foreach obj,$<,$(abspath ${obj}))
$(OUTDIR)/supp.o: supp.c lib.h
	cc -c -o $@ $< $(CFLAGS)

supp: $(OUTDIR)/supp.a
