#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
USFSTL_PATH := ../..
USFSTL_TEST_OBJS := test.o
USFSTL_TEST_NAME := test
USFSTL_BIN_PATH := lib
USFSTL_TEST_BIN_PATH := bin
USFSTL_TESTED_LIB := lib.a
USFSTL_SUPPORT_LIB := supp.a
USFSTL_CC_OPT := -I../supp/ -Wall -Wextra -Wno-unused-parameter
#USFSTL_CC_OPT += -m32 # also supported if you need
# use = instead of := because of $(USFSTL_TEST_CFG)
USFSTL_TEST_CC_OPT = $(USFSTL_CC_OPT) -I../lib/ -DCONFIG_$(USFSTL_TEST_CFG)=1
USFSTL_VHOST_USER := 1
USFSTL_SCHED_CTRL := 1

USFSTL_TESTED_FILES := code.c

USFSTL_TEST_CONFIGS := A B

include $(USFSTL_PATH)/core.mak

.PRECIOUS: $(USFSTL_BIN_PATH)/%/
$(USFSTL_BIN_PATH)/%/:
	mkdir -p $@
.PRECIOUS: $(USFSTL_BIN_PATH)/support-%/
$(USFSTL_BIN_PATH)/support-%/:
	mkdir -p $@

# mark everything phony so we really invoke the sub-makefile,
# which knows how to check if the library needs to be rebuilt,
# we don't ...
.PHONY: phony

.PRECIOUS: $(USFSTL_BIN_PATH)/%/$(USFSTL_TESTED_LIB)
$(USFSTL_BIN_PATH)/%/$(USFSTL_TESTED_LIB): phony | $(USFSTL_BIN_PATH)/%/
	make -C ../lib/					\
		CONFIG=$*				\
		OUTDIR=$(abspath $(dir $@))		\
		CFLAGS="$(USFSTL_CC_OPT) -mfentry -pg"	\
		lib

.PRECIOUS: $(USFSTL_BIN_PATH)/support-%/$(USFSTL_SUPPORT_LIB)
$(USFSTL_BIN_PATH)/support-%/$(USFSTL_SUPPORT_LIB): phony | $(USFSTL_BIN_PATH)/support-%/
	make -C ../supp/ 				\
		CONFIG=$*				\
		OUTDIR=$(abspath $(dir $@))		\
		CFLAGS="$(USFSTL_CC_OPT)"		\
		supp
