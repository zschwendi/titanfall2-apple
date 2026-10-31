# Native runtime (docs/RUNTIME.md).  Included by the root Makefile; every rule of the runtime
# lives here.  See runtime/README.md.
#
#   make rt                              build build/runtime/librt.a
#   make rt-test [SHIM=<lib>/<Name>]     build and run the runtime tests: all, or one
#   make rt-test-tsan [SHIM=<lib>/<Name>]  the same under ThreadSanitizer (build/runtime-tsan)
#   make shim-check SHIM=<lib>/<Name>    the "done" check for one shim (RUNTIME.md section 10.5)
#   make method-check METHOD=<Interface>/<Method>   the "done" check for one method body of the do-nothing device
#   make back-check ROW=<id>             the "done" check for one row of the back-end seam (rt/d3dback.h)
#   make core-test-check PIECE=<piece>   the "done" checks of a core piece written as two tasks
#   make core-body-check PIECE=<piece>   (tools/swarm/core_check.py; the table: make_core_tasks.py)
#   make rt-imports                      regenerate runtime/tools/imports.def from the inventory
#   make com-desc | com-desc-check       regenerate, or compare, the interface descriptions of runtime/com
#   make rt-clean                        remove build/runtime and build/gen
#
# Variables:
#   RT_BASE_REF   branch a worker's worktree was cut from; shim-check item 1 compares against
#                 the merge base with it (default wip/lifter-v0)
#   RT_BASE       an explicit base commit for item 1, instead of the merge base
#   SHIM_FAMILY   comma-separated <lib>/<Name> of the other shims a worker was given with this
#                 one; shim-check item 1 lets their files differ from the base as well
#   RT_TEST_ARGS  extra arguments for the test runner, for example -v
#   RT_TEST_TIMEOUT  (environment) seconds of wall-clock time each test gets; default 300
#   RT_TEST_SKIP  (environment) a file of tests not to run, `lib/Name  reason` to a line
#   RT_SANITIZE   build with -fsanitize=<this> (rt-test-tsan sets thread)
#   V=1           show the full compiler command lines
#   RT_OUT, RT_GEN  output directories (default build/runtime and build/gen), for building
#                 twice at once in one worktree
#   GUEST_MEM=seqcst  a diagnostic build of lifted code with sequentially consistent guest
#                 memory, in directories of its own (see below); GUEST_X16=atomic|plain with it
#   GUEST_STACK=plain  with GUEST_MEM only: guest accesses through [rsp] are plain, not ordered
#                 (see below); builds in directories of its own, e.g. build/gate3-tso-stackplain
#   GUEST_PATCHES=off  a diagnostic build from lifted code WITHOUT the guest patches, in
#                 directories of its own (see below)

# GUEST_MEM=seqcst: a DIAGNOSTIC build of lifted code in which guest loads and stores are
# sequentially consistent atomics (harness/runtime.h, TF_GUEST_MEM_SEQCST, which says what it
# is for and what it leaves out).  It builds beside the normal one, in build/runtime-seqcst,
# build/gen-seqcst and build/gate2-seqcst, so the two can be run against each other:
#     make gate2-build GUEST_MEM=seqcst && build/gate2-seqcst/gate2 call 29
#
# GUEST_X16=atomic or GUEST_X16=plain, with GUEST_MEM=seqcst only: how that build does the
# 16-byte loads and stores of the vector moves (harness/runtime.h, TF_GUEST_X16_ATOMIC and
# TF_GUEST_X16_PLAIN).  Without it they are two ordered 8-byte accesses; `atomic` makes one
# at a 16-aligned address a single 128-bit atomic access, `plain` leaves them as in the normal
# build.  Each builds in directories of its own (build/gate2-seqcst-x16atomic, ...):
#     make gate2-build GUEST_MEM=seqcst GUEST_X16=atomic
GUEST_MEM   ?=
GUEST_X16   ?=
# GUEST_STACK=plain, with GUEST_MEM=seqcst or tso only: the accesses to the running thread's own
# stack (an operand that is [rsp] with no index and no segment override, and push, pop, call, ret,
# leave, pushfq, popfq) are plain copies, as in the normal build, and the rest stay as GUEST_MEM
# makes them (harness/runtime.h, TF_GUEST_STACK_PLAIN and the STS64/LDS64 family).  That is one
# switch to test whether the ordering of guest stack accesses matters; the risk is a stack object
# another thread reads or writes whose owner's accesses are rsp-based.
GUEST_STACK ?=
# GUEST_MEM=tsob: acquire loads and `dmb ishst` before plain stores, x86's order without the release
# store's wait for earlier loads (harness/runtime.h, TF_GUEST_MEM_TSOB); the build without an Apple account.
# GUEST_MEM=tso: the same build with acquire loads and release stores (ldapr, stlr), which is
# x86's own order and no stronger (harness/runtime.h, TF_GUEST_MEM_TSO); build/runtime-tso, ... .
ifneq ($(filter seqcst tso tsob,$(GUEST_MEM)),)
ifneq ($(filter-out atomic plain,$(GUEST_X16)),)
$(error GUEST_X16=$(GUEST_X16): the values are atomic and plain)
endif
ifneq ($(filter-out plain,$(GUEST_STACK)),)
$(error GUEST_STACK=$(GUEST_STACK): the value is plain)
endif
ifneq ($(filter-out relaxed stonly ldonly,$(GUEST_DIAG)),)
$(error GUEST_DIAG=$(GUEST_DIAG): the values are relaxed, stonly and ldonly)
endif
GUEST_SUFFIX := -$(GUEST_MEM)$(if $(GUEST_X16),-x16$(GUEST_X16))$(if $(GUEST_STACK),-stack$(GUEST_STACK))$(if $(GUEST_DIAG),-diag$(GUEST_DIAG))
RT_OUT      ?= build/runtime$(GUEST_SUFFIX)
RT_GEN      ?= build/gen$(GUEST_SUFFIX)
else
ifneq ($(GUEST_MEM),)
$(error GUEST_MEM=$(GUEST_MEM): the values are seqcst, tso and tsob)
endif
ifneq ($(GUEST_X16),)
$(error GUEST_X16=$(GUEST_X16) needs GUEST_MEM=seqcst or tso)
endif
ifneq ($(GUEST_STACK),)
$(error GUEST_STACK=$(GUEST_STACK) needs GUEST_MEM=seqcst or tso)
endif
GUEST_SUFFIX :=
endif

# GUEST_PATCHES=off: a DIAGNOSTIC build from code lifted WITHOUT the guest patches of
# lifter/guest_patches.toml (the places where the lifted code is made to differ from the
# game's on purpose; lifter/guest_patches.py).  It is the game's own behaviour, kept buildable
# so that it can be measured against the patched one.  The lifter writes that C to
# build/lifted-unpatched/<module>, never to lifted/<module>, and everything made from it lands
# in directories with -unpatched in their names (build/runtime-unpatched, build/gate2-unpatched,
# build/gate3-unpatched; it combines with GUEST_MEM):
#     make gate2-build GUEST_PATCHES=off && build/gate2-unpatched/gate2 call 29
GUEST_PATCHES ?=
ifeq ($(GUEST_PATCHES),off)
GUEST_SUFFIX := $(GUEST_SUFFIX)-unpatched
RT_OUT      ?= build/runtime$(GUEST_SUFFIX)
RT_GEN      ?= build/gen$(GUEST_SUFFIX)
GUEST_LIFTED_DIR := build/lifted-unpatched
else
ifneq ($(GUEST_PATCHES),)
$(error GUEST_PATCHES=$(GUEST_PATCHES): the only value is off)
endif
GUEST_LIFTED_DIR := lifted
endif

# GUEST_WATCH=<address>:<value>: a DIAGNOSTIC build of lifted code that traps where it is about
# to store that 64-bit value at that guest address (harness/runtime.h, TF_WATCH_ST64_ADDR).
# Give it directories of its own:
#     make gate2-build GUEST_WATCH=0x1000019a540:0 RT_OUT=build/runtime-watch RT_GEN=build/gen-watch GATE2_OUT=build/gate2-watch
GUEST_WATCH ?=

RT_CC       ?= xcrun clang
RT_CXX      ?= $(RT_CC) -x c++
RT_AR       ?= xcrun ar
RT_PLATFORM ?= macos
# Kept separate for isolated layout diagnostics; the selected ranges must be
# available on the target OS (the iOS ranges are not portable to every Mac).
# A different layout must use distinct RT_OUT and RT_GEN build directories.
RT_VM_LAYOUT ?= $(if $(filter ios,$(RT_PLATFORM)),ios,macos)
RT_OUT      ?= build/runtime
RT_GEN      ?= build/gen
RT_SANITIZE ?=
RT_SAN_FLAGS := $(if $(RT_SANITIZE),-fsanitize=$(RT_SANITIZE) -fno-omit-frame-pointer)
RT_CFLAGS   := -std=gnu17 -fno-strict-aliasing -Wall -Wextra -Werror -O2 -g \
               -Iruntime/include -Iharness -I$(RT_GEN) $(RT_SAN_FLAGS)
ifeq ($(RT_VM_LAYOUT),ios)
RT_CFLAGS += -DRT_VM_LAYOUT_IOS=1
else ifneq ($(RT_VM_LAYOUT),macos)
$(error RT_VM_LAYOUT must be macos or ios)
endif
RT_OBJC_CFLAGS := $(RT_CFLAGS) -fobjc-arc
# The frameworks of the host window: on the link line of every program that links librt.a.
RT_FRAMEWORKS := -framework AppKit -framework Foundation -framework QuartzCore -framework Metal -framework CoreGraphics -framework AudioToolbox -framework GameController -lobjc
RT_BASE_REF ?= wip/lifter-v0
RT_BASE     ?=
RT_Q        := $(if $(V),,@)

# The make that ships with macOS (GNU Make 3.81) compares file times in whole seconds, so a
# file changed in the same second in which its object was built looks up to date, and the
# stale object is linked.  Every rule that produces something therefore ends by waiting for
# the clock to pass into the next second: whatever is edited after make returns is strictly
# newer than everything make built.  Costs half a second on average, only when something was
# rebuilt.
RT_SETTLE    = @$(PY) -c "import time; t = time.time(); time.sleep(1.02 - (t - int(t)))"

RT_TOOLS     := runtime/tools/gen_shims.py runtime/tools/shimdef.py
RT_COM_TOOLS := runtime/tools/gen_com.py $(RT_TOOLS)

# ---- sources ------------------------------------------------------------------------------
# A shim registers itself by existing: every runtime/shims/<lib>/<Name>.c is built and
# every runtime/tests/shims/<lib>/<Name>.c is a test.  Nothing else lists them.

RT_CORE_SRC  := $(sort $(wildcard runtime/core/*.c))
RT_SHIM_SRC  := $(sort $(wildcard runtime/shims/*/*.c))
# The host window (rt/host.h): Objective-C, compiled with -fobjc-arc, so it has its own list and rule.
RT_HOST_SRC := $(sort $(wildcard runtime/host/*.m))
ifeq ($(RT_PLATFORM),ios)
RT_HOST_SRC := $(filter-out runtime/host/window.m,$(RT_HOST_SRC)) runtime/host_ios/window.m
RT_FRAMEWORKS := -framework UIKit -framework Foundation -framework QuartzCore -framework Metal -framework CoreGraphics -framework AudioToolbox -framework GameController -framework AVFAudio -lobjc
else ifneq ($(RT_PLATFORM),macos)
$(error RT_PLATFORM must be macos or ios)
endif
RT_HOST_OBJ := $(patsubst %.m,$(RT_OUT)/obj/%.o,$(RT_HOST_SRC))
# The host's plain C (runtime/host/*.c: the speakers' sink, rt/host_audio.h): AudioToolbox, no Objective-C.
RT_HOST_C_SRC := $(sort $(wildcard runtime/host/*.c))
RT_HOST_C_OBJ := $(patsubst %.c,$(RT_OUT)/obj/%.o,$(RT_HOST_C_SRC))

# Stand-ins for EA's SDK (rt/standin.h): runtime/standins/<lib>/<Name>.c, in the format of a
# shim and made by the same generator, but into a table of their own (rt_standin_table) that
# is NOT part of rt_shim_table: a program has a stand-in only if it hands the row to rt_init.
# Their tests are runtime/tests/standins/*.c.
RT_STANDIN_SRC    := $(sort $(wildcard runtime/standins/*/*.c))
RT_STANDINGEN_SRC := $(RT_GEN)/standin_thunks.c $(RT_GEN)/standin_table.c

# The interface layer (rt/com.h; docs/handoff/d3d-interface-layer.md).  The descriptions are the checked-in
# runtime/com/desc/<Interface>.idef; a method body registers itself by existing, like a shim:
# every runtime/nulldev/<Interface>/<Method>.c is the body of that method, and every
# runtime/tests/nulldev/<Interface>/<Method>.c is a test.  runtime/nulldev/*.c is the object
# model of the do-nothing Direct3D device.  runtime/tests/com is a second, test-only set of
# interfaces with bodies, which the test program registers itself (as it does `rttest`).
# runtime/tests/back/<Interface>/<Method>.c is the test of that method's forward through the seam
# behind the device (rt/d3dback.h, rt/test_back.h): it registers itself by existing too.
RT_COM_DESC      := $(sort $(wildcard runtime/com/desc/*.idef))
RT_COM_DEFS      := runtime/tools/types.def runtime/com/types.def runtime/com/accept.def runtime/com/guard.def
RT_NULLDEV_SRC   := $(sort $(wildcard runtime/nulldev/*.c) $(wildcard runtime/nulldev/*/*.c))
# runtime/xaudio: the sound classes (CoCreateInstance's class table, runtime/xaudio/classes.c) and the
# bodies of the sound interfaces (runtime/xaudio/<Interface>/<Method>.c), read by gen_com as a second --bodies.
RT_XAUDIO_SRC   := $(sort $(wildcard runtime/xaudio/*.c) $(wildcard runtime/xaudio/*/*.c))
RT_TCOM_DESC     := $(sort $(wildcard runtime/tests/com/desc/*.idef))
RT_TCOM_DEFS     := runtime/tools/types.def runtime/tests/com/types.def runtime/tests/com/accept.def
RT_TCOM_SRC      := $(sort $(wildcard runtime/tests/com/bodies/*/*.c))

RT_GEN_SRC   := $(RT_GEN)/shim_thunks.c $(RT_GEN)/shim_table.c
RT_COMGEN_SRC := $(RT_GEN)/com_thunks.c $(RT_GEN)/com_table.c
RT_LIB_OBJ   := $(patsubst %.c,$(RT_OUT)/obj/%.o,$(RT_CORE_SRC) $(RT_SHIM_SRC) $(RT_NULLDEV_SRC) $(RT_XAUDIO_SRC) $(RT_STANDIN_SRC) $(RT_GEN_SRC) $(RT_COMGEN_SRC) $(RT_STANDINGEN_SRC))
RT_LIB_CHECK := $(RT_OUT)/obj/$(RT_GEN)/shim_types_check.o

# The test program: the runner, the tests, and the test-only library `rttest` (shims that
# exist to exercise the generator and the harness: runtime/tests/abi).
RT_ABI_SRC     := $(sort $(wildcard runtime/tests/abi/*/*.c))
RT_TEST_SRC    := runtime/tests/harness.c runtime/tests/loader_fixtures.c runtime/tests/com_fixtures.c $(wildcard runtime/tests/pe_build.c) \
                  $(sort $(wildcard runtime/tests/shims/*/*.c) $(wildcard runtime/tests/core/*.c) \
                         $(wildcard runtime/tests/nulldev/*/*.c) $(wildcard runtime/tests/back/*/*.c) \
                         $(wildcard runtime/tests/standins/*.c))
RT_TESTGEN_SRC := $(RT_GEN)/rttest_thunks.c $(RT_GEN)/rttest_table.c $(RT_GEN)/rttest_types_check.c
RT_TCOMGEN_SRC := $(RT_GEN)/rttest_com_thunks.c $(RT_GEN)/rttest_com_table.c
RT_TEST_OBJ    := $(patsubst %.c,$(RT_OUT)/obj/%.o,$(RT_TEST_SRC) $(RT_ABI_SRC) $(RT_TCOM_SRC) $(RT_TESTGEN_SRC) $(RT_TCOMGEN_SRC))

# ---- lifted modules ---------------------------------------------------------------------------
# The lifted C of a module becomes part of a host program in three steps (runtime/core/loader.c):
#   1. a `lifted <file> dll=<path> [module=<name>]` line in the program's module list (the
#      format of runtime/modules.def), from which gen_shims.py writes the module-table row
#      with the DLL's identity and a pointer to the module's function table;
#   2. $(call rt_lifted_rules,<module>,<directory holding the lifter's output>) below, which
#      makes $(RT_OUT)/lifted/<module>.o from every <module>_*.c there;
#   3. the program links that object and passes the table to rt_init (RtConfig.extra_modules).
# librt.a itself contains no lifted module, so the tests of the runtime never need a game file.
# The -mllvm options raise limits of the compiler's own analyses, which a lifted function exceeds
# by its size (thousands of uses of the one `ctx` pointer, hundreds of blocks).  The first is the
# one that matters: past 100 uses of a pointer the compiler stops asking whether it escapes and
# assumes it does, and then `restrict` on `ctx` and TF_NOESCAPE (harness/runtime.h, "THE REGISTER
# FILE DOES NOT ESCAPE") mean nothing: every guest store may have changed every register.  The
# others let dead-store elimination and load forwarding look as far as such a function is long.
# Measured on the hottest translation unit (client_0063.c): 87,347 to 60,604 instructions, and
# its compile takes less time than before, there being less code (docs/findings/25-lifted-speed.md).
# tools/liftcache/liftcache.py (DEFAULT_CFLAGS) and its selftest carry the same string.
RT_LIFT_CFLAGS ?= -O2 -g0 -fno-strict-aliasing -fwrapv -mllvm -capture-tracking-max-uses-to-explore=1000000 -mllvm -dse-memoryssa-scanlimit=1500 -mllvm -dse-memoryssa-walklimit=1500 -mllvm -dse-memoryssa-path-check-limit=300 -mllvm -memssa-check-limit=1500 -mllvm -memdep-block-scan-limit=2000 -mllvm -memdep-block-number-limit=2000 -Iharness
ifneq ($(filter seqcst tso tsob,$(GUEST_MEM)),)
RT_LIFT_CFLAGS += -DTF_GUEST_MEM_SEQCST $(if $(filter tso tsob,$(GUEST_MEM)),-DTF_GUEST_MEM_TSO) $(if $(filter tsob,$(GUEST_MEM)),-DTF_GUEST_MEM_TSOB)
# GUEST_DIAG= (with GUEST_MEM only), a MEASURING build that is wrong without the processor's x86 mode, so run
# it signed with RT_TSO=require: what the ordering of the guest accesses costs, apart from the atomics.
#   relaxed  loads and stores relaxed atomics (plain ldr/str, still atomics to the compiler)
#   stonly   loads relaxed, stores release (stlr)
#   ldonly   loads acquire (ldapr), stores relaxed
ifeq ($(GUEST_DIAG),relaxed)
RT_LIFT_CFLAGS += -DTF_GUEST_LOAD_ORDER=__ATOMIC_RELAXED -DTF_GUEST_STORE_ORDER=__ATOMIC_RELAXED
endif
ifeq ($(GUEST_DIAG),stonly)
RT_LIFT_CFLAGS += -DTF_GUEST_LOAD_ORDER=__ATOMIC_RELAXED -DTF_GUEST_STORE_ORDER=__ATOMIC_RELEASE
endif
ifeq ($(GUEST_DIAG),ldonly)
RT_LIFT_CFLAGS += -DTF_GUEST_LOAD_ORDER=__ATOMIC_ACQUIRE -DTF_GUEST_STORE_ORDER=__ATOMIC_RELAXED
endif
ifeq ($(GUEST_X16),atomic)
RT_LIFT_CFLAGS += -DTF_GUEST_X16_ATOMIC
endif
ifeq ($(GUEST_X16),plain)
RT_LIFT_CFLAGS += -DTF_GUEST_X16_PLAIN
endif
ifeq ($(GUEST_STACK),plain)
RT_LIFT_CFLAGS += -DTF_GUEST_STACK_PLAIN
endif
endif
ifneq ($(GUEST_WATCH),)
RT_LIFT_CFLAGS += -DTF_WATCH_ST64_ADDR=$(word 1,$(subst :, ,$(GUEST_WATCH)))ull -DTF_WATCH_ST64_VALUE=$(word 2,$(subst :, ,$(GUEST_WATCH)))ull
endif
define rt_lifted_rules
$$(RT_OUT)/lifted/$(1).o: $(2)/$(1).report.json $$(wildcard harness/*.h harness/insns/*.h)
	@mkdir -p $$(RT_OUT)/lifted/$(1)
	@rm -f $$(RT_OUT)/lifted/$(1)/*.o
	$$(if $$(V),,@echo "  LIFT  $(1)")
	$$(RT_Q)for c in $(2)/$(1)_*.c; do \
	    $$(RT_CC) $$(RT_LIFT_CFLAGS) -I$(2) -c $$$$c -o $$(RT_OUT)/lifted/$(1)/$$$$(basename $$$$c .c).o || exit 1; \
	done
	$$(RT_Q)ld -r -o $$@ $$(RT_OUT)/lifted/$(1)/*.o
	$$(RT_SETTLE)
endef

# The test program's one real lifted module: the test corpus built at -O1 (tests/corpus/build.sh;
# the DLLs are not in git, a worktree has the main checkout's through a link).  It is lifted
# into the build directory whenever the DLL or the lifter changes.
RT_CORPUS_DLL := tests/corpus/build/corpus_O1.dll
$(RT_OUT)/lift/corpus_O1/corpus_O1.report.json: $(RT_CORPUS_DLL) $(wildcard lifter/*.py lifter/insns/*.py)
	@mkdir -p $(RT_OUT)/lift
	$(PY) -m lifter.lift --module corpus_O1 --dll $(RT_CORPUS_DLL) --out $(RT_OUT)/lift/corpus_O1 > $(RT_OUT)/lift/corpus_O1.log 2>&1 \
	    || { cat $(RT_OUT)/lift/corpus_O1.log; exit 1; }
	$(RT_SETTLE)
$(eval $(call rt_lifted_rules,corpus_O1,$(RT_OUT)/lift/corpus_O1))
RT_TEST_LIFTED := $(RT_OUT)/lifted/corpus_O1.o

.PHONY: host-audio-tone rt rt-test rt-net-lint rt-test-build rt-test-tsan shim-check method-check core-test-check core-body-check rt-test-pending rt-imports rt-clean RT_FORCE com-desc com-desc-check d3d-layout-check

rt: $(RT_OUT)/librt.a

# Cross-platform port check: force every archive member into a link, so an
# unsupported framework or unresolved runtime symbol cannot hide in the archive.
.PHONY: rt-link-check
rt-link-check: $(RT_OUT)/rt-link-check
$(RT_OUT)/rt-link-check: runtime/tools/linkcheck.c $(RT_OUT)/librt.a
	$(RT_Q)$(RT_CC) $(RT_CFLAGS) $< -Wl,-force_load,$(RT_OUT)/librt.a $(RT_FRAMEWORKS) -o $@

# ---- generated code (never committed) -------------------------------------------------------
# The list of shim files is a dependency, so that adding or deleting a shim regenerates the
# tables even though no remaining file is newer.

$(RT_GEN)/.sources: RT_FORCE
	@mkdir -p $(RT_GEN)
	@echo "$(RT_SHIM_SRC) | $(RT_ABI_SRC) | $(RT_COM_DESC) | $(RT_NULLDEV_SRC) | $(RT_XAUDIO_SRC) | $(RT_TCOM_DESC) | $(RT_TCOM_SRC) | $(RT_STANDIN_SRC)" | cmp -s - $@ || \
	    echo "$(RT_SHIM_SRC) | $(RT_ABI_SRC) | $(RT_COM_DESC) | $(RT_NULLDEV_SRC) | $(RT_XAUDIO_SRC) | $(RT_TCOM_DESC) | $(RT_TCOM_SRC) | $(RT_STANDIN_SRC)" > $@

$(RT_GEN)/.shims.stamp: $(RT_SHIM_SRC) $(RT_TOOLS) runtime/tools/types.def runtime/com/types.def runtime/modules.def $(RT_GEN)/.sources
	$(PY) runtime/tools/gen_shims.py --shims runtime/shims --types runtime/tools/types.def --types runtime/com/types.def \
	    --modules runtime/modules.def --out $(RT_GEN)
	@touch $@
	$(RT_SETTLE)

$(RT_GEN)/.rttest.stamp: $(RT_ABI_SRC) $(RT_TOOLS) runtime/tools/types.def runtime/tests/abi/types.def \
                         runtime/tests/abi/modules.def $(RT_GEN)/.sources $(RT_CORPUS_DLL)
	$(PY) runtime/tools/gen_shims.py --shims runtime/tests/abi --types runtime/tools/types.def \
	    --types runtime/tests/abi/types.def --modules runtime/tests/abi/modules.def --out $(RT_GEN) \
	    --prefix rttest --table rttest_shim_table --module-table rttest_module_defs --allow-unchecked
	@touch $@
	$(RT_SETTLE)

# The stand-ins' table (rt/standin.h).  The generator writes a module table with every table
# of shims; this one's (rt_standin_module_defs, a second copy of runtime/modules.def) is used
# by nothing.
$(RT_GEN)/.standins.stamp: $(RT_STANDIN_SRC) $(RT_TOOLS) runtime/tools/types.def runtime/modules.def $(RT_GEN)/.sources
	$(PY) runtime/tools/gen_shims.py --shims runtime/standins --types runtime/tools/types.def \
	    --modules runtime/modules.def --out $(RT_GEN) \
	    --prefix standin --table rt_standin_table --module-table rt_standin_module_defs
	@touch $@
	$(RT_SETTLE)

# The interface layer's thunks and tables: the runtime's own set, and the test program's.
$(RT_GEN)/.com.stamp: $(RT_COM_DESC) $(RT_COM_DEFS) $(RT_NULLDEV_SRC) $(RT_XAUDIO_SRC) $(RT_COM_TOOLS) $(RT_GEN)/.sources
	$(PY) runtime/tools/gen_com.py --desc runtime/com/desc --types runtime/tools/types.def --types runtime/com/types.def \
	    --accept runtime/com/accept.def --guard runtime/com/guard.def --bodies runtime/nulldev --bodies runtime/xaudio --out $(RT_GEN)
	@touch $@
	$(RT_SETTLE)

$(RT_GEN)/.rttest_com.stamp: $(RT_TCOM_DESC) $(RT_TCOM_DEFS) $(RT_TCOM_SRC) $(RT_COM_TOOLS) $(RT_GEN)/.sources
	$(PY) runtime/tools/gen_com.py --desc runtime/tests/com/desc --types runtime/tools/types.def --types runtime/tests/com/types.def \
	    --accept runtime/tests/com/accept.def --bodies runtime/tests/com/bodies --out $(RT_GEN) \
	    --prefix rttest_com --table rttest_com_table
	@touch $@
	$(RT_SETTLE)

# Every generated file, headers included, is declared as made by its stamp, so that make
# looks at its time after the generator ran and not before.
$(RT_COMGEN_SRC) $(RT_GEN)/com_ids.h $(RT_GEN)/com_types.h $(RT_GEN)/com_calls.h: $(RT_GEN)/.com.stamp ;
$(RT_TCOMGEN_SRC) $(RT_GEN)/rttest_com_ids.h $(RT_GEN)/rttest_com_types.h $(RT_GEN)/rttest_com_calls.h: $(RT_GEN)/.rttest_com.stamp ;
$(RT_GEN_SRC) $(RT_GEN)/shim_types_check.c $(RT_GEN)/shim_ids.h $(RT_GEN)/shim_calls.h: $(RT_GEN)/.shims.stamp ;
$(RT_TESTGEN_SRC) $(RT_GEN)/rttest_ids.h $(RT_GEN)/rttest_calls.h: $(RT_GEN)/.rttest.stamp ;
$(RT_STANDINGEN_SRC) $(RT_GEN)/standin_ids.h $(RT_GEN)/standin_calls.h: $(RT_GEN)/.standins.stamp ;

# ---- compiling --------------------------------------------------------------------------------

$(RT_LIB_OBJ) $(RT_LIB_CHECK): $(RT_OUT)/obj/%.o: %.c | $(RT_GEN)/.shims.stamp $(RT_GEN)/.com.stamp $(RT_GEN)/.standins.stamp
	@mkdir -p $(dir $@)
	$(if $(V),,@echo "  CC    $<")
	$(RT_Q)$(RT_CC) $(RT_CFLAGS) -MMD -MP -c $< -o $@

$(RT_TEST_OBJ): $(RT_OUT)/obj/%.o: %.c | $(RT_GEN)/.shims.stamp $(RT_GEN)/.rttest.stamp $(RT_GEN)/.com.stamp $(RT_GEN)/.rttest_com.stamp $(RT_GEN)/.standins.stamp
	@mkdir -p $(dir $@)
	$(if $(V),,@echo "  CC    $<")
	$(RT_Q)$(RT_CC) $(RT_CFLAGS) -Iruntime/tests/abi -DRT_TEST_CORPUS_DLL='"$(abspath $(RT_CORPUS_DLL))"' -MMD -MP -c $< -o $@

# Joins the archive after the .c rule above: that rule's targets are the list it was given when it was read.
RT_LIB_OBJ  += $(RT_HOST_OBJ) $(RT_HOST_C_OBJ)
$(RT_HOST_C_OBJ): $(RT_OUT)/obj/%.o: %.c | $(RT_GEN)/.shims.stamp
	@mkdir -p $(dir $@)
	$(if $(V),,@echo "  CC    $<")
	$(RT_Q)$(RT_CC) $(RT_CFLAGS) -MMD -MP -c $< -o $@
$(RT_HOST_OBJ): $(RT_OUT)/obj/%.o: %.m | $(RT_GEN)/.shims.stamp
	@mkdir -p $(dir $@)
	$(if $(V),,@echo "  CC    $<")
	$(RT_Q)$(RT_CC) $(RT_OBJC_CFLAGS) -MMD -MP -c $< -o $@

# The archive is rebuilt from the current list of objects, so a deleted source leaves nothing behind.
$(RT_OUT)/librt.a: $(RT_LIB_OBJ) $(RT_LIB_CHECK)
	@rm -f $@
	$(if $(V),,@echo "  AR    $@")
	$(RT_Q)$(RT_AR) rcs $@ $(RT_LIB_OBJ)
	$(RT_SETTLE)

$(RT_OUT)/rt-test: $(RT_TEST_OBJ) $(RT_TEST_LIFTED) $(RT_OUT)/librt.a
	$(if $(V),,@echo "  LINK  $@")
	$(RT_Q)$(RT_CC) $(RT_SAN_FLAGS) -o $@ $(RT_TEST_OBJ) $(RT_TEST_LIFTED) $(RT_OUT)/librt.a $(RT_FRAMEWORKS)
	$(RT_SETTLE)

-include $(RT_LIB_OBJ:.o=.d) $(RT_LIB_CHECK:.o=.d) $(RT_TEST_OBJ:.o=.d)

# ---- running ------------------------------------------------------------------------------------

rt-test-build: $(RT_OUT)/rt-test

# The link-level half of "no socket of the guest reaches the network" (rt/hostsock.h) runs with
# every rt-test: first the lint's own test (it must fail on an object that asks for socket()),
# then the lint over the runtime's archive.
rt-net-lint: $(RT_OUT)/librt.a
	$(RT_Q)sh runtime/tools/host_net_lint.sh --self-test "$(RT_CC)" $(RT_OUT)/host_net_lint.tmp
	$(RT_Q)sh runtime/tools/host_net_lint.sh --archive $(RT_OUT)/librt.a

rt-test: $(RT_OUT)/rt-test rt-net-lint
	$(RT_OUT)/rt-test $(RT_TEST_ARGS) $(SHIM)

# The suite under ThreadSanitizer, built into its own directories.  The tests that cannot
# work under the sanitizer, each with its reason, are in runtime/tests/tsan.skip; the reports
# in runtime/core/ that are known and not yet dealt with are in runtime/tests/tsan.supp.  A
# test in which the sanitizer reports anything else fails (its process ends with status 66).
# die_after_fork=0: T_TRAPS forks from tests that have started threads.  The time limit is
# four times the usual one: the sanitizer makes the stress tests several times slower.
RT_TSAN_OUT := build/runtime-tsan
RT_TSAN_GEN := build/gen-tsan
rt-test-tsan:
	@$(MAKE) rt-test-build RT_OUT=$(RT_TSAN_OUT) RT_GEN=$(RT_TSAN_GEN) RT_SANITIZE=thread
	RT_TEST_TIMEOUT=$${RT_TEST_TIMEOUT:-1200} RT_TEST_SKIP=runtime/tests/tsan.skip \
	TSAN_OPTIONS="die_after_fork=0 suppressions=$(abspath runtime/tests/tsan.supp) $$TSAN_OPTIONS" \
	    $(RT_TSAN_OUT)/rt-test $(RT_TEST_ARGS) $(SHIM)

# The host window's test: a hidden window, synthetic events, nothing shown (runtime/tools/hostwin/test.m).
$(RT_OUT)/hostwin-test: runtime/tools/hostwin/test.m $(RT_OUT)/librt.a
	$(if $(V),,@echo "  LINK  $@")
	$(RT_Q)$(RT_CC) $(RT_OBJC_CFLAGS) -o $@ runtime/tools/hostwin/test.m $(RT_OUT)/librt.a $(RT_FRAMEWORKS)
host-window-test: $(RT_OUT)/hostwin-test
	$(RT_OUT)/hostwin-test

# The tone for the owner (runtime/tools/hostaudio/tone.c): built, NOT run.  Running it plays a second
# of 440 Hz from the Mac's speakers; only the owner runs it.
$(RT_OUT)/hostaudio-tone: runtime/tools/hostaudio/tone.c $(RT_OUT)/librt.a
	$(if $(V),,@echo "  LINK  $@")
	$(RT_Q)$(RT_CC) $(RT_CFLAGS) -o $@ runtime/tools/hostaudio/tone.c $(RT_OUT)/librt.a $(RT_FRAMEWORKS)
host-audio-tone: $(RT_OUT)/hostaudio-tone
	@echo "built $(RT_OUT)/hostaudio-tone (not run). To hear it, the owner runs:"
	@echo "  RT_HOST_AUDIO=coreaudio $(RT_OUT)/hostaudio-tone"

shim-check:
	@test -n "$(SHIM)" || { echo "usage: make shim-check SHIM=<lib>/<Name>"; exit 2; }
	$(PY) runtime/tools/check_shim.py $(SHIM) --base-ref "$(RT_BASE_REF)" $(if $(RT_BASE),--base "$(RT_BASE)") $(if $(SHIM_FAMILY),--family "$(SHIM_FAMILY)") --make "$(MAKE)"

# How the two check tools read the exit status of a program they ran (also run by `make test`).
.PHONY: check-tools-test
check-tools-test:
	$(PY) runtime/tools/check_shim.py --selftest
	$(PY) runtime/tools/check_method.py --selftest
test: check-tools-test

# The "done" check for one method body of the do-nothing device (runtime/tools/check_method.py):
#   make method-check METHOD=<Interface>/<Method> [METHOD_FAMILY=<Interface>/<Method>,...]
method-check:
	@test -n "$(METHOD)" || { echo "usage: make method-check METHOD=<Interface>/<Method>"; exit 2; }
	$(PY) runtime/tools/check_method.py $(METHOD) --base-ref "$(RT_BASE_REF)" $(if $(RT_BASE),--base "$(RT_BASE)") $(if $(METHOD_FAMILY),--family "$(METHOD_FAMILY)") --make "$(MAKE)"

# The "done" check for one row of the back-end seam (runtime/tools/check_back.py; tools/swarm/back_rows.py):
#   make back-check ROW=<id>
.PHONY: back-check
back-check:
	@test -n "$(ROW)" || { echo "usage: make back-check ROW=<id>   (tools/swarm/back_rows.py --list)"; exit 2; }
	$(PY) runtime/tools/check_back.py $(ROW) --base-ref "$(RT_BASE_REF)" $(if $(RT_BASE),--base "$(RT_BASE)") --make "$(MAKE)"

# ---- core pieces written as two tasks (tools/swarm/make_core_tasks.py) ----------------------------
#   make core-test-check PIECE=<piece>   "done" for the task that types the piece's test
#   make core-body-check PIECE=<piece>   "done" for the task that writes the body
#   make rt-test-pending PENDING=<file.c> PENDING_OUT=<dir> [PENDING_LINK=0]
#       one test file that is not in the suite, compiled exactly as the suite compiles a test
#       (as if it stood in runtime/tests/core/) and linked with the suite into <dir>/rt-test

core-test-check core-body-check:
	@test -n "$(PIECE)" || { echo "usage: make $@ PIECE=<piece>   (tools/swarm/make_core_tasks.py --list)"; exit 2; }
	$(PY) tools/swarm/core_check.py $(patsubst core-%-check,%,$@) $(PIECE) --base-ref "$(RT_BASE_REF)" $(if $(RT_BASE),--base "$(RT_BASE)") --make "$(MAKE)"

rt-test-pending: $(RT_OUT)/rt-test
	@test -n "$(PENDING)" -a -d "$(PENDING_OUT)" || { echo "usage: make rt-test-pending PENDING=<file.c> PENDING_OUT=<dir>"; exit 2; }
	$(RT_Q)$(RT_CC) $(RT_CFLAGS) -Iruntime/tests/abi -iquote runtime/tests/core -DRT_TEST_CORPUS_DLL='"$(abspath $(RT_CORPUS_DLL))"' -c $(PENDING) -o $(PENDING_OUT)/pending.o
	$(if $(filter 0,$(PENDING_LINK)),,$(RT_Q)$(RT_CC) -o $(PENDING_OUT)/rt-test $(RT_TEST_OBJ) $(PENDING_OUT)/pending.o $(RT_TEST_LIFTED) $(RT_OUT)/librt.a $(RT_FRAMEWORKS))

# The interface descriptions, from the MinGW-w64 headers of the DXMT clone (runtime/com/gen_desc.py).
# By hand only: a build reads the checked-in runtime/com/desc and runtime/com/types.def.
#   make com-desc          rewrite them          make com-desc-check    compare (skipped without the headers)
com-desc:
	$(PY) runtime/com/gen_desc.py
com-desc-check:
	$(PY) runtime/com/gen_desc.py --check
# rt/win/d3d11.h and rt/win/dxgi.h against the same headers: every size, offset and constant
# (runtime/tools/check_d3d_layout.py; skipped without the headers).
d3d-layout-check:
	$(PY) runtime/tools/check_d3d_layout.py

rt-imports:
	$(PY) runtime/tools/gen_imports.py inventory/inventory.json > runtime/tools/imports.def.tmp
	mv runtime/tools/imports.def.tmp runtime/tools/imports.def

rt-clean:
	rm -rf $(RT_OUT) $(RT_GEN)
