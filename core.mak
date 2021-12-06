#
# Copyright (C) 2018 - 2021 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
#
# USFSTL core Makefile
#
# To use this, include it into a Makefile in your project and
# define the following:
#  - USFSTL_PATH            = path to the usfstl files, also use it to include
#                             this core makefile like this:
#                             include $(USFSTL_PATH)/core.mak
#  - USFSTL_LOG_TO_FILES    = set to 1 if compile logging should go to files
#                           different files will be created:
#                            - tested-<config>.txt
#                            - support-<config>.txt
#                            - framework.txt
#                            - test-<config>-<name>.txt
#  - USFSTL_LOGDIR          = Log directory for compilations with USFSTL_LOG_TO_FILES
#                           (will be created if it doesn't exist)
#  - USFSTL_CC_OPT          = general CC options to be used for everything, e.g. -m32
#  - USFSTL_TESTED_LIB      = tested library filename (e.g. libtest.a)
#  - USFSTL_SUPPORT_LIB     = support library filename (e.g. libsupport.a)
#  - USFSTL_TEST_OBJS       = Test object files. Note that this is actually called
#                             as a function, being passed the current configuration
#                             as the only argument. This still works if it's just a
#                             list of files, but also lets users modify the list
#                             depending on the configuration.
#  - USFSTL_TESTSRC_DIR     = defaults to empty, set this if you have a need to
#                             manipulate the C files used for the USFSTL_TEST_OBJS
#                             and add an appropriate rule that depends on the real
#                             C file to do that
#  - USFSTL_TESTED_FILES    = tested C files that are OK to call from this test, not
#                             relevant in flow tests; this can also be configured in
#                             in each test's tested_files list in the C code.
#  - USFSTL_BIN_PATH        = Directory name where the usfstl/tested/support
#                             libraries should be compiled (in subdirectories each)
#  - USFSTL_TEST_BIN_PATH   = Path where the test binary will be compiled
#  - USFSTL_TEST_NAME       = Test binary file name, not including .exe suffix
#                             (where applicable)
#  - USFSTL_EXTRA_TEST_C_FILES = Optional, additional C files to compile into each
#                                test, can be useful e.g. for __asan_default_options()
#  - USFSTL_TEST_ENV_VARS   = Optional, if set must be (a list of) VAR=value for
#                             environment variables to set while running the test.
#  - USFSTL_CLEAN_FILES     = Optional, files that should be cleaned by "make clean"
#  - USFSTL_TEST_CONFIGS    = Optional, list of test configurations that this test
#                             needs to build, the binaries will be created like
#                               $(USFSTL_TEST_BIN_PATH)/<config>/$(USFSTL_TEST_NAME)[.exe]
#                             If you do not wish a test to be with multiple configs,
#                             leave this unset and we'll default to '.' so no extra
#                             subdirectory is created.
#  - USFSTL_RUN_CONFIGS     = Optional, list of test configurations that this test
#                             runs for, defaults to USFSTL_TEST_CONFIGS. It may need
#                             to be different if you need to build multiple configs
#                             but run only one, for example when different components
#                             integrate into the same simulation.
#  - USFSTL_TESTED_LIB_CFG  = Optional, tested library configuration string, suitable as
#                             a directory name. If you support multiple configurations
#                             per test (see USFSTL_TEST_CONFIGS) then you can use the helper
#                             variable $$(USFSTL_TEST_CFG) here to make this depend on the
#                             test configuration, which this defaults to.
#  - USFSTL_SUPPORT_LIB_CFG = Optional, support library configuration string, just like
#                             USFSTL_TESTED_LIB_CFG but for the support library, in case
#                             you have different kinds of tests/support libs for the same
#                             tested-lib configuration.
#  - USFSTL_TEST_ARGS       = optional, arguments passed to the test run. You can use a
#                             % character here and it'll be replaced by the configuration.
#
#  - USFSTL_TEST_CC_OPT     = CC options to compile the tests with
#  - USFSTL_TEST_LINK_OPT   = optional, defaults to USFSTL_TEST_CC_OPT, set
#                             this if linking requires different compiler
#                             arguments than compiling
#  - USFSTL_TEST_PRE_CC_CMD = If set, make this a command that will do
#                             something before the compiler runs, e.g. set
#                             an environment variable like so:
#                               CPATH="$$(cat /my/env/file)
#                             This can be useful for large projects where the
#                             whole CPATH doesn't fit into the 32k command
#                             line limit that Win32 has.
#  - USFSTL_CONTEXT_BACKEND = Context backend implementation, can be
#                              - pthread (available on Windows* and Linux)
#                              - ucontext (available on Linux*)
#                              - fiber (available on Windows)
#                             Defaults are marked with * above.
#  - USFSTL_FUZZING         = set to "1" to enable fuzzing, or "repro" to build only
#                             for running reproducers from a previous fuzzing run
#  - USFSTL_FUZZER          = One of afl-gcc, afl-clang-fast, libfuzzer. Currently
#                             defaults to afl-gcc for best compatibility.
#  - USFSTL_VHOST_USER      = if set, compile the vhost-user code to be able to
#                             export vhost-user devices.
#  - USFSTL_SCHED_CTRL      = if set, compile in the necessary code to interact with
#                             the user-mode-linux controller as a client
#  - USFSTL_DONTKEEP_LIB    = if set to 1, don't mark the intermediate project lib.a
#                             as precious, which means less space is required but
#                             slightly increases link time when only test changes
#                             are made
#  - USFSTL_SKIP_ASAN_STR   = skip ASAN on certain string functions used by the
#                             framework itself, set this to 1 if compiling the binary
#                             with ASAN but not the framework, to speed things up
#  - USFSTL_PERF_ARGS       = arguments for running perf, defaults to "-g"
#
# As the next step, include this file (core.mak), which will give you
# the necessary targets like "build" and "run".
#
# core.mak also provides the following variables to use in your makefile:
#  - S                    = evaluates to either nothing or @, to be used as a
#                           build command prefix to allow "make V=1" to print out
#                           all the commands executed. Use it!
#  - USFSTL_PATH_SEP        = PATH separator, ";" on Windows and ":" elsewhere
#  - USFSTL_LOG_TESTED      = suitable to put at the end of the build rule(s)
#                             for $(LIBTESTED) to log their output, use this
#                             to get proper logging into place
#                             When using this (or the next), make sure the targets
#                             have an order-only dependency on $(USFSTL_LOGDIR).
#  - USFSTL_LOG_SUPPORT     = suitable to put at the end of the build rule(s)
#                             for $(LIBSUPPORT) to log their output, use this
#                             to get proper logging into place
#  - USFSTL_TEST_SECTION    = the name of a .text section replacement for
#                             objects that are instrumented but could be called
#                             without explicitly stubbing them.
#
# Finally, your Makefile needs to provide the following targets:
#  - $(USFSTL_BIN_PATH)/tested-%/$(USFSTL_TESTED_LIB):
#	To build your tested library. Note that you need to build at least with
#	-fno-inline-small-functions, otherwise the stubbing system cannot work
#	right. Consider -O0.
#	If there are multiple configurations, then you should derive the
#	configuration from $*, which is the "%" part of the rule, which
#	is the configuration.
#  - $(USFSTL_BIN_PATH)/support-%/$(USFSTL_SUPPORT_LIB):
#	To build your support library. The same comments regarding using %* as
#	with the tested library apply.
#
# This needs to be done after including "core.mak" since these variables are
# defined only by core.mak.
#
# You must build both these targets with "-pg -mfentry" for the stubbing
# system to work - the support really only needs it if you might include
# files there that declare inline functions you might want to stub.
#

############ SETUP ############
_USFSTL_MACHINE := $(shell $(CC) -dumpmachine)
_USFSTL_M_CYGWIN := $(findstring cygwin,$(_USFSTL_MACHINE))
_USFSTL_M_MINGW := $(findstring mingw,$(_USFSTL_MACHINE))
ifeq ($(_USFSTL_M_CYGWIN)$(_USFSTL_M_MINGW),)
_USFSTL_WINDOWS := 0
_USFSTL_EXESUFFIX :=
USFSTL_CONTEXT_BACKEND ?= ucontext
else
_USFSTL_WINDOWS := 1
_USFSTL_EXESUFFIX := .exe
USFSTL_CONTEXT_BACKEND ?= pthread
endif

ifeq ($(findstring ;,$(PATH)),)
USFSTL_PATH_SEP := :
else
USFSTL_PATH_SEP := ";"
endif

# disable implicit rules - mostly this makes the
# debug output of make easier to read...
MAKEFLAGS += --no-builtin-rules
.SUFFIXES:

_USFSTL_LIB = $(_USFSTL_LIB_PATH)/libusfstl.a
_USFSTL_TEST_BINARY := $(USFSTL_TEST_NAME)$(_USFSTL_EXESUFFIX)
# (will override for fuzzing below)
_USFSTL_LIB_PATH := $(USFSTL_BIN_PATH)/usfstl/

ifeq ($(USFSTL_FUZZING),)
_USFSTL_FUZZING :=
else
ifeq ($(USFSTL_FUZZING),repro)
_USFSTL_FUZZING := REPRO
else
ifneq ($(USFSTL_FUZZING),1)
$(error wrong USFSTL_FUZZING option)
else

ifeq ($(USFSTL_FUZZER),)
USFSTL_FUZZER := afl-gcc
endif

ifeq ($(USFSTL_FUZZER),afl-gcc)
_USFSTL_AFL := 1
endif
ifeq ($(USFSTL_FUZZER),afl-gcc-fast)
_USFSTL_AFL := 1
endif
ifeq ($(USFSTL_FUZZER),afl-clang-fast)
_USFSTL_AFL := 1
endif

ifeq ($(_USFSTL_AFL),1)
_USFSTL_REPRO := $(findstring --fuzz-repro,$(USFSTL_TEST_ARGS))
ifeq ($(_USFSTL_REPRO),)
_USFSTL_TEST_RUNNER := $(USFSTL_PATH)/afl-fuzz.sh
endif
endif

ifeq ($(USFSTL_FUZZER),afl-gcc)
export CC=afl-gcc
_USFSTL_FUZZING := AFL_GCC
export AFL_SKIP_BIN_CHECK=1
else
ifeq ($(USFSTL_FUZZER),afl-clang-fast)
export CC=afl-clang-fast
_USFSTL_FUZZING := AFL_CLANG_FAST
else
ifeq ($(USFSTL_FUZZER),libfuzzer)
_USFSTL_FUZZING := LIB_FUZZER
export CC=clang-9
USFSTL_CC_OPT += -fsanitize=fuzzer
else
ifeq ($(USFSTL_FUZZER),afl-gcc-fast)
export CC=afl-gcc-fast
_USFSTL_FUZZING := AFL_GCC_FAST
else
$(error wrong USFSTL_FUZZER option)
endif # afl-gcc-fast
endif # libfuzzer
endif # afl-clang-fast
endif # afl-gcc

export AFL_QUIET=1
export AFL_DONT_OPTIMIZE=1
endif # USFSTL_FUZZING != 1
endif # USFSTL_FUZZING != repro
_USFSTL_LIB_PATH := $(USFSTL_BIN_PATH)/usfstl-fuzz$(USFSTL_FUZZING)/
endif # USFSTL_FUZZING != (empty)

_USFSTL_CC := $(CC)
_USFSTL_FINAL_CC := $(CC)

USFSTL_TEST_LINK_OPT ?= $(USFSTL_TEST_CC_OPT)
USFSTL_TEST_CONFIGS ?= .
USFSTL_RUN_CONFIGS ?= $(USFSTL_TEST_CONFIGS)
USFSTL_TESTSRC_DIR ?=
USFSTL_TEST_SECTION := text_test

ifeq ($(USFSTL_CONTEXT_BACKEND),pthread)
USFSTL_TEST_LINK_OPT += -lpthread
endif

USFSTL_TESTED_LIB_CFG ?= $$(USFSTL_TEST_CFG)
USFSTL_SUPPORT_LIB_CFG ?= $$(USFSTL_TEST_CFG)

ifeq ($(V),1)
S=
else
S=@
endif

test_cfg = $(word 1,$(subst /, ,$(subst $(USFSTL_TEST_BIN_PATH),,$1)))
USFSTL_TEST_CFG = $(call test_cfg,$(if $(_USFSTL_ONECFG),$(_USFSTL_ONECFG),$@))

ifeq ($(USFSTL_LOG_TO_FILES),1)
extract_cfg = $(word 1,$(subst /, ,$(subst $(USFSTL_BIN_PATH)/$2,,$1)))
support_cfg = $(call extract_cfg,$1,support-)
tested_cfg = $(call extract_cfg,$1,tested-)
test_cfg_prefix = $(word 1,$(subst /, ,$*))
_USFSTL_LOGFILE_TESTED   = $(USFSTL_LOGDIR)/tested-$(call tested_cfg,$@).txt
_USFSTL_LOGFILE_SUPPORT  = $(USFSTL_LOGDIR)/support-$(call support_cfg,$@).txt
_USFSTL_LOGFILE_TEST     = $(USFSTL_LOGDIR)/test-$(call test_cfg_prefix,$@)$(USFSTL_TEST_NAME).txt
_USFSTL_LOGFILE_USFSTL  := $(USFSTL_LOGDIR)/framework.txt
USFSTL_LOG_TESTED        = >> $(_USFSTL_LOGFILE_TESTED) 2>&1
USFSTL_LOG_SUPPORT       = >> $(_USFSTL_LOGFILE_SUPPORT) 2>&1
USFSTL_LOG_TEST          = >> $(_USFSTL_LOGFILE_TEST) 2>&1
USFSTL_LOG_USFSTL       := >> $(_USFSTL_LOGFILE_USFSTL) 2>&1
else
USFSTL_LOG_TESTED       :=
USFSTL_LOG_SUPPORT      :=
USFSTL_LOG_TEST         :=
USFSTL_LOG_USFSTL       :=
endif

ASSERT_PROFILING_DEFINE = -DUSFSTL_USE_ASSERT_PROFILING=1
USFSTL_CC_OPT += $(ASSERT_PROFILING_DEFINE)
USFSTL_TEST_CC_OPT += $(ASSERT_PROFILING_DEFINE)

# We really need:
# -g -gdwarf-2                   to get debug information we process here
#                                we use v2 because v3,v4 crash gdb when
#                                run in slick edit debugger
#
# workarounds:
# -mno-ms-bitfields              fixes an issue with GCC's packing: something
#                                like "U16, U32, U16" doesn't get packed
#                                properly on mingw, cf. gcc bugzilla 52991.
USFSTL_CC_OPT += -g -gdwarf-2 -mno-ms-bitfields
USFSTL_TEST_CC_OPT += -g -gdwarf-2 -mno-ms-bitfields -DUSFSTL_TEST_NAME=$(USFSTL_TEST_NAME)
# -m32, -mfentry and -fpic aren't compatible, so if we have -m32 add -fno-pic
ifeq ($(filter -m32,$(USFSTL_CC_OPT)),-m32)
USFSTL_CC_OPT += -fno-pic
_USFSTL_GLOBAL_PACK = LL
else
_USFSTL_GLOBAL_PACK = QQ
endif

COMMA := ,
_USFSTL_TESTCODE_DEFINES := "-DUSFSTL_TESTED_FILES=$(patsubst %,\"%\"$(COMMA),$(USFSTL_TESTED_FILES))"

############ USER TARGETS ############
.PHONY: build
build: $(addprefix $(USFSTL_TEST_BIN_PATH)/,$(addsuffix /$(_USFSTL_TEST_BINARY).globals,$(USFSTL_TEST_CONFIGS)))

RUN=
_ARGS_GDB=--multi-debug-subprocs
GDB=gdb --args
USFSTL_PERF_ARGS ?= -g
PERF=perf record $(USFSTL_PERF_ARGS)
LIST=
_ARGS_LIST=--list | sed 's/^\(.*\):.*/CFG=%:\1/'
RUNCFG=
_ARGS_RUNCFG=--test=$(lastword $(subst :, ,$(CFG)))
_RUNCFG=$(firstword $(subst :, ,$(CFG)))

.PHONY: run-%
run-%: build
	@echo " $(word 1,$(subst @, ,$*))  $(USFSTL_TEST_BIN_PATH)/$(word 2,$(subst @, ,$*))/$(_USFSTL_TEST_BINARY) $(subst %,$(word 2,$(subst @, ,$*)),$(USFSTL_TEST_ARGS)) $(subst %,$(word 2,$(subst @, ,$*)),$(_ARGS_$(word 1,$(subst @, ,$*))))"
	$(S)$(USFSTL_TEST_ENV_VARS) $(_USFSTL_TEST_RUNNER) $($(word 1,$(subst @, ,$*))) $(USFSTL_TEST_BIN_PATH)/$(word 2,$(subst @, ,$*))/$(_USFSTL_TEST_BINARY) $(subst %,$(word 2,$(subst @, ,$*)),$(USFSTL_TEST_ARGS)) $(subst %,$(word 2,$(subst @, ,$*)),$(_ARGS_$(word 1,$(subst @, ,$*))))

.PHONY: run-cfg
run-cfg: build $(addprefix run-RUNCFG@,$(_RUNCFG))

.PHONY: run
run: build $(addprefix run-RUN@,$(USFSTL_RUN_CONFIGS))

.PHONY: gdb
gdb: build $(addprefix run-GDB@,$(USFSTL_RUN_CONFIGS))

perf: build $(addprefix run-PERF@,$(USFSTL_RUN_CONFIGS))

.PHONY: list-cfg
list-cfg: build $(addprefix run-LIST@,$(USFSTL_RUN_CONFIGS))

.PHONY: clean
clean:
	$(S)rm -rf $(foreach _USFSTL_ONECFG,$(USFSTL_TEST_CONFIGS),$(eval _CFG=$(USFSTL_BIN_PATH)/tested-$(USFSTL_TESTED_LIB_CFG)/)$(_CFG))
	$(S)rm -rf $(foreach _USFSTL_ONECFG,$(USFSTL_TEST_CONFIGS),$(eval _CFG=$(USFSTL_BIN_PATH)/support-$(USFSTL_SUPPORT_LIB_CFG)/)$(_CFG))
	$(S)rm -rf $(USFSTL_TEST_BIN_PATH) $(_USFSTL_LIB_PATH)
	$(S)rm -f *.gcno gmon.out
	$(S)rm -f $(USFSTL_CLEAN_FILES)

usfstl: $(_USFSTL_LIB)

.PHONY: tested
tested: $(foreach _USFSTL_ONECFG,$(USFSTL_TEST_CONFIGS),$(eval _CFG=$(USFSTL_BIN_PATH)/tested-$(USFSTL_TESTED_LIB_CFG)/$(USFSTL_TESTED_LIB).md5)$(_CFG))

.PHONY: support
support: $(foreach _USFSTL_ONECFG,$(USFSTL_TEST_CONFIGS),$(eval _CFG=$(USFSTL_BIN_PATH)/support-$(USFSTL_SUPPORT_LIB_CFG)/$(USFSTL_SUPPORT_LIB).md5)$(_CFG))

############ HELPER TARGETS ############
# We used to have a simple "%.md5: %" rule instead, but make refuses
# to use it twice for different targets - basically what happens is
# this:
#  test          depends on   libsupport.a.md5
#  %.md5         depends on   %                (rule marked as used)
#  libsupport.a  depends on   libtested.a.md5
#  --- %.md5 rule is not considered (marked as used) to find libtested.a
#
# work around this by splitting the rule.
.PRECIOUS: %/$(USFSTL_TESTED_LIB).md5
%/$(USFSTL_TESTED_LIB).md5: %/$(USFSTL_TESTED_LIB)
	$(S)mkdir -p $(dir $@)
	$(S)md5sum $< 2>/dev/null | cmp -s $@ -; if test $$? -ne 0; then md5sum $< > $@; fi
.PRECIOUS: %/$(USFSTL_SUPPORT_LIB).md5
%/$(USFSTL_SUPPORT_LIB).md5: %/$(USFSTL_SUPPORT_LIB)
	$(S)mkdir -p $(dir $@)
	$(S)md5sum $< 2>/dev/null | cmp -s $@ -; if test $$? -ne 0; then md5sum $< > $@; fi

$(USFSTL_LOGDIR):
	$(S)mkdir -p $@
$(_USFSTL_LIB_PATH)/dwarf/:
	$(S)mkdir -p $@
.PRECIOUS: $(USFSTL_TEST_BIN_PATH)/%/
$(USFSTL_TEST_BIN_PATH)/%/:
	$(S)mkdir -p $@
$(USFSTL_TEST_BIN_PATH):
	$(S)mkdir -p $@
.PRECIOUS: $(USFSTL_TEST_BIN_PATH)/%/
$(USFSTL_TEST_BIN_PATH)/%/:
	$(S)mkdir -p $@
$(USFSTL_BIN_PATH)/%/:
	$(S)mkdir -p $@

############ USFSTL BUILD ############
OBJS = print.o main.o override.o dwarf.o testrun.o restore.o fuzz.o opt.o
OBJS += ctx-$(USFSTL_CONTEXT_BACKEND).o ctx-common.o sched.o task.o rpc.o
OBJS += multi.o multi-rpc.o multi-ctl.o multi-ptc.o multi-shared-mem.o rpc-rpc.o loop.o alloc.o
OBJS += assert-profiling.o string.o rand.o
ASM_OBJS = entry.o
DWARF_OBJS = dwarf/dwarf.o dwarf/sort.o dwarf/state.o dwarf/fileline.o
DWARF_READ_OBJS = dwarf/posix.o dwarf/backtrace.o
ifeq ($(_USFSTL_WINDOWS),1)
OBJS += watchdog-win32.o rpc-win32.o multi-win32.o
DWARF_OBJS += dwarf/pecoff.o dwarf/nounwind.o dwarf/alloc.o
DWARF_READ_OBJS += dwarf/read.o
USFSTL_TEST_LINK_OPT += -lws2_32
else
OBJS += watchdog-posix.o rpc-posix.o multi-posix.o wallclock.o
ifneq ($(USFSTL_VHOST_USER),)
# include PCI since it just requires vhost, no point separating
OBJS += vhost.o uds.o pci.o
endif # USFSTL_VHOST_USER
ifneq ($(USFSTL_SCHED_CTRL),)
OBJS += uds.o schedctrl.o
endif # USFSTL_SCHED_CTRL
DWARF_OBJS += dwarf/elf.o dwarf/mmap.o
DWARF_READ_OBJS += dwarf/mmapio.o
endif
OBJS += $(DWARF_OBJS) $(DWARF_READ_OBJS)

_USFSTL_CC_INC := -I$(USFSTL_PATH)/include/ -I.
_USFSTL_CC_OPT = $(USFSTL_CC_OPT) -Wall -Wextra -Wno-unused-parameter -Wno-format-zero-length -Werror -D_FILE_OFFSET_BITS=64
_USFSTL_AS_OPT := $(filter-out -mno-ms-bitfields,$(_USFSTL_CC_OPT))
_USFSTL_CC_OPT += -DHAVE_DL_ITERATE_PHDR=1 -D_GNU_SOURCE=1 -DHAVE_ATOMIC_FUNCTIONS -DHAVE_SYNC_FUNCTIONS
_USFSTL_CC_OPT += -DHAVE_DECL_STRNLEN=1
ifeq ($(findstring clang,$(CC)),clang)
# usfstl actually uses this gnu extension, so don't warn on it in clang.
_USFSTL_CC_OPT += -Wno-gnu-variable-sized-type-not-at-end
endif
ifeq ($(USFSTL_SKIP_ASAN_STR),1)
_USFSTL_CC_OPT += -DUSFSTL_WANT_NO_ASAN_STRING=1
USFSTL_TEST_LINK_OPT += -ldl
endif
ifneq ($(_USFSTL_FUZZING),)
_USFSTL_CC_OPT += -DUSFSTL_USE_FUZZING -DUSFSTL_FUZZER_$(_USFSTL_FUZZING)
endif

$(_USFSTL_LIB_PATH)/%.o: $(USFSTL_PATH)/src/%.c | $(_USFSTL_LIB_PATH)/dwarf/ $(USFSTL_LOGDIR)
	@echo " CC   usfstl/$(notdir $<)" $(USFSTL_LOG_USFSTL)
	$(S)$(_USFSTL_CC) -c -MMD -MP $(_USFSTL_CC_INC) $(_USFSTL_CC_OPT) -o $@ $< $(USFSTL_LOG_USFSTL)
$(_USFSTL_LIB_PATH)/entry.o: $(USFSTL_PATH)/src/entry.S | $(_USFSTL_LIB_PATH)/dwarf/ $(USFSTL_LOGDIR)
$(_USFSTL_LIB_PATH)/entry.o: $(USFSTL_PATH)/src/entry-*.s
$(_USFSTL_LIB_PATH)/%.o: $(USFSTL_PATH)/src/%.S | $(_USFSTL_LIB_PATH)/dwarf/ $(USFSTL_LOGDIR)
	@echo " AS   usfstl/$(notdir $<)" $(USFSTL_LOG_USFSTL)
	$(S)$(_USFSTL_CC) -c $(_USFSTL_AS_OPT) $(_USFSTL_CC_INC) $< -o $@ $(USFSTL_LOG_USFSTL)
$(_USFSTL_LIB): $(addprefix $(_USFSTL_LIB_PATH)/,$(OBJS))
$(_USFSTL_LIB): $(addprefix $(_USFSTL_LIB_PATH)/,$(ASM_OBJS))
$(_USFSTL_LIB): | $(USFSTL_LOGDIR)
	@echo " AR   $(notdir $@)" $(USFSTL_LOG_USFSTL)
	$(S)rm -f $@
	$(S)ar rcs $@ $^ $(USFSTL_LOG_USFSTL)

############ TEST BUILD ############

# there's a bug in mingw nm - unless --size-sort is given it doesn't output the size
# There are read only global vars which are relocatable.
# Those won't considered as RO by the nm command since they should be located on runtime.
# Remove those vars from the globals which should be restored using address filtering.
$(USFSTL_TEST_BIN_PATH)/%/$(_USFSTL_TEST_BINARY).globals: $(USFSTL_TEST_BIN_PATH)/%/$(_USFSTL_TEST_BINARY)
	@echo " GEN  $*/$(notdir $@)" $(USFSTL_LOG_TEST)
	$(eval DATA_RO_PARAMS:= $(shell objdump -h $< | grep ".data.rel.ro" | cut -d' ' -f5-7))
	$(eval DATA_RO_ADDR:= $(word 2, $(DATA_RO_PARAMS)))
	$(eval DATA_RO_SIZE:= $(word 1, $(DATA_RO_PARAMS)))
	$(eval DATA_RO_END:= $(shell echo $$((0x$(DATA_RO_ADDR) + 0x$(DATA_RO_SIZE)))))
	$(eval DATA_RO_END_HEX:= $(shell printf '%08x' $(DATA_RO_END)))
	$(S)nm -S --size-sort $< | sort | \
		awk '{if ("$(DATA_RO_ADDR)" == "" || "$(DATA_RO_END_HEX)" == "" || $$1 < "$(DATA_RO_ADDR)" || $$1 >= "$(DATA_RO_END_HEX)") print}' | \
		grep -E -v " . (_*_gcov|_*emutls|.*_(l|a|ub)san|\.bss|\.data|___|__end__|_Z.*(GlobCopy|pglob_copy|scandir|qsort)|_Z.*__(sanitizer|interception)|replaced_headers|__usfstl_assert_info|usfstl_tested_files|__unnamed)" | \
		perl -ne 'binmode(stdout); m/^([0-9a-f]*) ([0-9a-f]*) [dDbB] .*/ && print pack("$(_USFSTL_GLOBAL_PACK)",hex($$1), hex($$2))' > $@

.SECONDEXPANSION:
# Sometimes there are cross-dependencies between tested and support,
# work those out by linking them together into an archive first.
ifneq ($(USFSTL_DONTKEEP_LIB),1)
.PRECIOUS: $(USFSTL_TEST_BIN_PATH)/%/$(_USFSTL_TEST_BINARY).a
endif
$(USFSTL_TEST_BIN_PATH)/%/$(_USFSTL_TEST_BINARY).a: \
                                                $(USFSTL_BIN_PATH)/tested-$(USFSTL_TESTED_LIB_CFG)/$(USFSTL_TESTED_LIB).md5 \
						$(USFSTL_BIN_PATH)/support-$(USFSTL_SUPPORT_LIB_CFG)/$(USFSTL_SUPPORT_LIB).md5 \
						$(_USFSTL_LIB) \
						| $(USFSTL_TEST_BIN_PATH)/%/ $(USFSTL_LOGDIR)
	@echo " AR   $*/$(notdir $@)" $(USFSTL_LOG_TEST)
	$(S)rm -f $@
	$(S)(echo create $@ ; \
             for f in $(patsubst %.md5,%,$^) ; do \
                case "$$f" in \
                    *.o) echo addmod $$f ;; \
                    *.a) echo addlib $$f ;; \
                esac ; \
	     done; \
	     echo save ; echo end) | ar -M $(USFSTL_LOG_TEST)
	$(S)ranlib $@ $(USFSTL_LOG_TEST)

_usfstl_get_objs = $(addprefix $(USFSTL_TEST_BIN_PATH)/$1/,$(call USFSTL_TEST_OBJS,$1))

.PRECIOUS: $(USFSTL_TEST_BIN_PATH)/%/$(_USFSTL_TEST_BINARY)
.SECONDEXPANSION:
$(USFSTL_TEST_BIN_PATH)/%/$(_USFSTL_TEST_BINARY): $(USFSTL_EXTRA_TEST_C_FILES) \
						  $$(call _usfstl_get_objs,%) \
						  $(USFSTL_TEST_BIN_PATH)/%/$(_USFSTL_TEST_BINARY).a \
						  | $(USFSTL_TEST_BIN_PATH)/%/ $(USFSTL_LOGDIR)
	@echo " LD   $*/$(notdir $@)" $(USFSTL_LOG_TEST)
	$(S)$(USFSTL_TEST_PRE_CC_CMD) $(_USFSTL_FINAL_CC) $(_USFSTL_CC_INC) $^ $(USFSTL_TEST_LINK_OPT) -o $@ $(USFSTL_LOG_TEST)
	$(S)if [ "$(_USFSTL_FUZZING)" = "AFL_GCC" ] ; then sed -i s/__AFL_SHM_ID/__AFL_SHM_IX/g $@ ; fi

# need to also compile this with "-pg -mfentry" because the test might
# include a header file here that has an inline it also wants to stub,
# and that is used by another inline that should be tested
.PRECIOUS: $(USFSTL_TEST_BIN_PATH)/%.o.tmp
$(USFSTL_TEST_BIN_PATH)/%.o.tmp: $(USFSTL_TESTSRC_DIR)$$(notdir %).c \
				 $(USFSTL_BIN_PATH)/tested-$(USFSTL_TESTED_LIB_CFG)/$(USFSTL_TESTED_LIB).md5 \
				 $(USFSTL_BIN_PATH)/support-$(USFSTL_SUPPORT_LIB_CFG)/$(USFSTL_SUPPORT_LIB).md5 \
				 | $(USFSTL_LOGDIR) $$(dir $(USFSTL_TEST_BIN_PATH)/%)
	@echo " CC   $(dir $*)$(notdir $<)" $(USFSTL_LOG_TEST)
	$(S)$(USFSTL_TEST_PRE_CC_CMD) $(CC) -MMD -MP -pg -mfentry -c $(_USFSTL_CC_INC) $(USFSTL_TEST_CC_OPT) $(_USFSTL_TESTCODE_DEFINES) $< -o $@ $(USFSTL_LOG_TEST)

.PRECIOUS: $(USFSTL_TEST_BIN_PATH)/%.o
$(USFSTL_TEST_BIN_PATH)/%.o: $(USFSTL_TEST_BIN_PATH)/%.o.tmp
	@echo " REL  $(dir $*)$(notdir $<)" $(USFSTL_LOG_TEST)
	$(S)objcopy --rename-section=.text=$(USFSTL_TEST_SECTION) $< $@

_USFSTL_TEST_OBJ_DEPS := $(patsubst %,%.d,$(foreach _CFG,$(USFSTL_TEST_CONFIGS),$(call _usfstl_get_objs,$(_CFG)))) $(OBJS:%.o=$(_USFSTL_LIB_PATH)/%.d)
-include $(_USFSTL_TEST_OBJ_DEPS)
