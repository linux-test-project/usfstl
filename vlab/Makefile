#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
MAKEFLAGS += --no-print-directory -l$(shell nproc)

ALL=controller wmediumd/wmediumd/wmediumd iw/iw linux/linux

all: $(ALL)

# support V=1
ifeq ($(V),1)
Q=
else
Q=@
endif

# linux
linux:
	$(Q)git clone https://git.kernel.org/pub/scm/linux/kernel/git/jberg/um.git linux
	$(Q)cd linux && git checkout v5.8+fixes
linux/.config: linux uml-kernel.config
	$(Q)cp uml-kernel.config linux/.config
linux/linux: linux/.config
	$(Q)(cd linux && $(MAKE) ARCH=um linux modules scripts_gdb)

# time controller
.PHONY: controller
controller:
	$(Q)cd ../control/ && $(MAKE)

# wmediumd
wmediumd:
	$(Q)git clone https://github.com/bcopeland/wmediumd
wmediumd/wmediumd/wmediumd: wmediumd
	$(Q)cd wmediumd && $(MAKE) SANITIZE=1

# iw
iw:
	$(Q)git clone https://git.kernel.org/pub/scm/linux/kernel/git/jberg/iw.git
iw/iw: iw
	$(Q)cd iw && $(MAKE)

.PHONY: pychecks
pychecks:
	$(Q)mypy virtlab.py plugins/*.py
	$(Q)pylint virtlab.py plugins/*.py

EXAMPLES := $(sort $(wildcard examples/*.sh))
TESTRUNS := $(patsubst examples/%,run_test_%,$(EXAMPLES))
TEST_LOGDIR := logs/$(shell date +%Y-%d-%m-%H_%M_%S)
run_test_%: $(ALL)
	@echo "------------- running $(patsubst run_test_%,%,$@) -------------"
	@mkdir -p "$(TEST_LOGDIR)/$(patsubst run_test_%,%,$@)"
	@VLAB_ARGS="--capture-all --logpath=$(TEST_LOGDIR)/$(patsubst run_test_%,%,$@)" ./examples/$(patsubst run_test_%,%,$@)
tests: $(TESTRUNS)
