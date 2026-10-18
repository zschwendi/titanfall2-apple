# The Gate 3 driver (gate3/driver.c; docs/handoff/gate3-build.md): the lifted DLLs of
# gate3/modules.def in one program: the 20 of the boot path and, since 11 October, the pak
# loader's ui(11).dll (21 modules).  Included by the root Makefile after runtime/rt.mk and
# gate2/gate2.mk, whose variables it uses.
#
#   make gate3-build      the lifted modules from the shared cache (built there when missing),
#                         the driver, the link: build/gate3/gate3
#   make gate3-registry   gate3 registry
#   make gate3-load       gate3 load: every module in a process of its own (LOAD="engine client"
#                         for some of them)
#   make gate3-run        gate3 run: our own entry point calls the launcher's main function (ARGS="..."
#                         for a game command line other than the default one of gate3/run.c)
#   make gate3-check      the test that this keeps working: build, run `registry`, check that every
#                         module of gate3/bands.def (21) is present with a function count inside
#                         its band and that the registry has no other lifted module; then the
#                         host-socket lint on the program, and gate3/check_origin_fence.sh: a
#                         run whose guard for EA's SDK says "bad" is refused before any module
#                         of the game is loaded (skipped, exit 0, where game/ has no DLLs)
#   make gate3-keys       each module's cache key and whether the cache has it
#   make gate3-stats      what building each cached module cost
#   make gate3-prune      remove all but the two most recently used cache entries per module
#   make liftcache-test   the cache's self-test (also run by `make test`)
#   make gate3-state-test the test of gate3/state.c: a run's start and what it leaves (no game
#                         file; also run by `make test` and `make gate3-check`)
#
# THE LIFTED MODULES ARE NOT BUILT IN THE WORKTREE.  tools/liftcache/liftcache.py keeps them,
# lifted C and objects, under .cache/lifted of the main checkout, one entry per module and
# key; the key covers the DLL, the lifter, the headers under harness/, RT_LIFT_CFLAGS and the
# compiler.  A worktree that changes none of these links the cached objects and compiles
# nothing of them.  build/gate3/lifted/<module> is a link to the lifted C of the entry in use.
#
# Variables: GATE3_JOBS (processes the cache build runs at once, lifter and compiler together;
# 6), LIFT_CACHE (the cache directory), GATE3_OUT.  GUEST_MEM=seqcst (runtime/rt.mk) builds
# build/gate3-seqcst/gate3 from cache entries of its own, and GUEST_PATCHES=off
# build/gate3-unpatched/gate3 from entries lifted without the guest patches.
#
# Game files: read here by the lifter (through liftcache.py), by gen_shims.py (identity of
# each DLL, for the module table) and by the driver when it runs.  Everything built lands in
# build/ or in the cache, which are git-ignored and must stay so.

GATE3_OUT      ?= build/gate3$(GUEST_SUFFIX)
GATE3_GEN      := $(GATE3_OUT)/gen
GATE3_BIN      := $(GATE3_OUT)/gate3
GATE3_MANIFEST := gate3/modules.def
GATE3_DLLS     := $(shell sed -n 's/^lifted .* dll=\([^ ]*\).*/\1/p' $(GATE3_MANIFEST))
GATE3_HAVE_GAME := $(if $(filter-out $(wildcard $(GATE3_DLLS)),$(GATE3_DLLS)),,yes)
GATE3_JOBS     ?= 6
GATE3_LIFTED   := $(GATE3_OUT)/lifted
LIFTCACHE      := $(PY) tools/liftcache/liftcache.py
LIFTCACHE_ARGS  = --manifest $(GATE3_MANIFEST) --cflags '$(RT_LIFT_CFLAGS)' --cc '$(RT_CC)' $(if $(LIFT_CACHE),--cache '$(LIFT_CACHE)') \
                  $(if $(filter off,$(GUEST_PATCHES)),--no-patches)

.PHONY: gate3-build gate3-registry gate3-load gate3-run gate3-check gate3-keys gate3-stats gate3-prune

gate3-prune:
	$(LIFTCACHE) prune $(if $(LIFT_CACHE),--cache '$(LIFT_CACHE)')

# The cache's own test: the test corpus through a cache of its own (no game file; about 20 s).
# Part of `make test`.
.PHONY: liftcache-test
liftcache-test:
	$(PY) tools/liftcache/selftest.py
test: liftcache-test

# The test of gate3/state.c (where a run starts and what it leaves behind: gate3/state.h):
# plain host files in a scratch directory, no game file and no runtime.  Part of `make test`
# and of `make gate3-check`.
.PHONY: gate3-state-test
gate3-state-test:
	@mkdir -p $(GATE3_OUT)/state-test
	$(RT_Q)$(RT_CC) -std=c11 -Wall -Wextra -Werror -O1 -o $(GATE3_OUT)/state-test/state_test gate3/tests/state_test.c gate3/state.c
	@$(GATE3_OUT)/state-test/state_test $(abspath $(GATE3_OUT)/state-test/scratch)
test: gate3-state-test

ifeq ($(GATE3_HAVE_GAME),yes)

# ---- the lifted modules: from the cache ----------------------------------------------------------
# Runs every time: the keys are computed from the files (a fraction of a second when nothing
# changed).  objects.filelist is rewritten only when an object's path changes, so the link
# below happens exactly when a module's key changed.
$(GATE3_LIFTED)/objects.filelist: RT_FORCE
	@mkdir -p $(GATE3_LIFTED)
	$(RT_Q)$(LIFTCACHE) ensure $(LIFTCACHE_ARGS) --out $(GATE3_LIFTED) --jobs $(GATE3_JOBS)

gate3-keys:
	@$(LIFTCACHE) key $(LIFTCACHE_ARGS)

gate3-stats:
	@$(LIFTCACHE) stats $(LIFTCACHE_ARGS)

# ---- the driver's module table ---------------------------------------------------------------------
# As for Gate 2: no --shims directory, so the generated shim table is the terminator row and
# every import resolves against librt.a's own tables.
$(GATE3_GEN)/.stamp: $(GATE3_MANIFEST) $(RT_TOOLS) runtime/tools/types.def $(GATE3_DLLS)
	@mkdir -p $(GATE3_GEN)
	$(PY) runtime/tools/gen_shims.py --types runtime/tools/types.def --modules $(GATE3_MANIFEST) \
	    --out $(GATE3_GEN) --prefix gate3 --table gate3_shim_table --module-table gate3_module_defs
	@touch $@
	$(RT_SETTLE)
$(GATE3_GEN)/gate3_table.c $(GATE3_GEN)/gate3_ids.h: $(GATE3_GEN)/.stamp ;

# ---- the driver --------------------------------------------------------------------------------------
# Its own files, and the two of the Gate 2 driver it shares (gate2/modes.c, gate2/peek.c).
GATE3_CFLAGS := $(RT_CFLAGS) -I$(GATE3_GEN) -Igate3 -Igate2 \
                -DGATE3_INSTALL_ROOT='"$(abspath game)"' -DGATE3_STATE_DIR='"$(abspath $(GATE3_OUT)/state)"' \
                -DGATE3_RUN_OUT='"$(abspath $(GATE3_OUT)/run)"'
GATE3_SRC    := $(sort $(wildcard gate3/*.c)) gate2/modes.c gate2/peek.c
GATE3_OBJ    := $(patsubst %.c,$(GATE3_OUT)/obj/%.o,$(GATE3_SRC)) $(GATE3_OUT)/obj/gate3_table.o

$(GATE3_OUT)/.flags: RT_FORCE
	@mkdir -p $(GATE3_OUT)
	@echo '$(GATE3_CFLAGS)' | cmp -s - $@ || echo '$(GATE3_CFLAGS)' > $@

# The runtime's generated headers come first: gate3/run.c includes rt/nulldev.h (com_ids.h) and the
# stand-ins' table (standin_ids.h), and in a fresh worktree nothing else has made them yet.
$(GATE3_OUT)/obj/%.o: %.c $(GATE3_OUT)/.flags | $(GATE3_GEN)/.stamp $(RT_GEN)/.shims.stamp $(RT_GEN)/.com.stamp $(RT_GEN)/.standins.stamp
	@mkdir -p $(dir $@)
	$(if $(V),,@echo "  CC    $<")
	$(RT_Q)$(RT_CC) $(GATE3_CFLAGS) -MMD -MP -c $< -o $@

$(GATE3_OUT)/obj/gate3_table.o: $(GATE3_GEN)/gate3_table.c $(GATE3_OUT)/.flags
	@mkdir -p $(dir $@)
	$(if $(V),,@echo "  CC    $<")
	$(RT_Q)$(RT_CC) $(GATE3_CFLAGS) -c $< -o $@

# The renderer's back end (runtime/dxback, DXB_OBJS of runtime/replay.mk) is linked in when its objects
# exist. Its object DXB_LOADER is the DXMT loader: the one input of the link that asks for dlopen and
# dlsym (the lint's --dxmt-loader form, gate3-check below). The DXMT libraries it loads:
DXB_LOADER      := $(RT_OUT)/obj/runtime/dxback/dxback.o
DXMT_DYLIB_DIRS := third_party/dxmt/build-native third_party/dxmt/build-native-assert $(wildcard build/dxmt/Frameworks)

# The list of the back end's objects, rewritten only when it changes: a link without the back end
# (no DXMT headers) must relink the program that had it, and removing a prerequisite does not do that.
$(GATE3_OUT)/.dxb: RT_FORCE
	@mkdir -p $(GATE3_OUT)
	@echo '$(DXB_OBJS)' | cmp -s - $@ || echo '$(DXB_OBJS)' > $@

$(GATE3_BIN): $(GATE3_OBJ) $(DXB_OBJS) $(GATE3_OUT)/.dxb $(GATE3_LIFTED)/objects.filelist $(RT_OUT)/librt.a
	$(if $(V),,@echo "  LINK  $@")
	$(RT_Q)$(RT_CC) -o $@ $(GATE3_OBJ) $(DXB_OBJS) -Wl,-filelist,$(GATE3_LIFTED)/objects.filelist $(RT_OUT)/librt.a $(DXB_LIBS) $(RT_FRAMEWORKS)
	$(RT_SETTLE)

-include $(GATE3_OBJ:.o=.d)

gate3-build: $(GATE3_BIN)

gate3-registry: $(GATE3_BIN)
	$(GATE3_BIN) registry

gate3-load: $(GATE3_BIN)
	$(GATE3_BIN) load $(LOAD)

gate3-run: $(GATE3_BIN)
	$(GATE3_BIN) run $(if $(ARGS),-- $(ARGS))

gate3-check: $(GATE3_BIN) gate3-state-test
	@$(GATE3_BIN) registry > $(GATE3_OUT)/registry.log; s=$$?; grep '^module \|^gate3 registry' $(GATE3_OUT)/registry.log; \
	[ $$s = 0 ] || { echo "gate3-check: FAILED: gate3 registry ended with status $$s"; exit 1; }; \
	$(PY) gate3/check_registry.py gate3/bands.def $(GATE3_OUT)/registry.log
	@if [ -n "$(DXB_OBJS)" ]; then \
	    { printf '%s\n' $(GATE3_OBJ) $(filter-out $(DXB_LOADER),$(DXB_OBJS)) $(RT_OUT)/librt.a; cat $(GATE3_LIFTED)/objects.filelist; } > $(GATE3_OUT)/lint-inputs.txt; \
	    sh runtime/tools/host_net_lint.sh --program $(GATE3_BIN) --dxmt-loader $(DXB_LOADER) --inputs $(GATE3_OUT)/lint-inputs.txt \
	        --dxmt-libs "$$(find $(DXMT_DYLIB_DIRS) -name '*.dylib' 2>/dev/null | sort | tr '\n' ' ')" || exit 1; \
	else sh runtime/tools/host_net_lint.sh --program $(GATE3_BIN) || exit 1; fi
	@sh gate3/check_origin_fence.sh $(GATE3_BIN) $(GATE3_OUT)/origin-fence

else

# A clone without the game: nothing can be lifted, generated or run.
gate3-check:
	@echo "gate3-check: SKIPPED: no $(firstword $(filter-out $(wildcard $(GATE3_DLLS)),$(GATE3_DLLS))) here (a checkout without the game's files); nothing was built or run"

gate3-build gate3-registry gate3-load gate3-run gate3-keys gate3-stats:
	@echo "$@: needs the DLLs of $(GATE3_MANIFEST), and $(firstword $(filter-out $(wildcard $(GATE3_DLLS)),$(GATE3_DLLS))) is not there"; exit 1

endif
