# The Gate 2 driver (gate2/driver.c; docs/handoff/gate2-build.md).  Included by the root
# Makefile after runtime/rt.mk, whose variables and rt_lifted_rules it uses.
#
#   make gate2-build      lift tier0 and vstdlib if needed, compile the lifted C, link build/gate2/gate2
#   make gate2-registry   gate2 registry: what rt_init makes of the two modules
#   make gate2            registry, load and call, in that order, then one summary line
#   make gate2-check      the test that this keeps working: build, run `registry`, check its numbers
#                         (skipped, exit 0, where game/ has no DLLs)
#   make gate2-diag ...   a diagnostic driver with the C of one guest instruction replaced (below)
#
# Game files: the only things here that read game/ are the lifter (`make lift`), gen_shims.py
# (size, timestamp and hash of each DLL, for the module table) and the driver when it runs
# (the runtime's loader).  Everything this file builds lands in build/ or lifted/, which are
# git-ignored and must stay so.

# GUEST_MEM=seqcst (runtime/rt.mk) builds a second driver, build/gate2-seqcst/gate2, and
# GUEST_X16=atomic or plain with it a third and a fourth (build/gate2-seqcst-x16atomic/gate2).
# GUEST_PATCHES=off (runtime/rt.mk) builds build/gate2-unpatched/gate2 from code lifted without
# the guest patches, into build/lifted-unpatched; gate2/queue_runs.py counts one against the other.
GATE2_OUT  ?= build/gate2$(GUEST_SUFFIX)
GATE2_GEN  := $(GATE2_OUT)/gen
GATE2_BIN  := $(GATE2_OUT)/gate2
GATE2_MODS := tier0 vstdlib
GATE2_DLL   = game/bin/x64_retail/$(1).dll
GATE2_DLLS := $(foreach m,$(GATE2_MODS),$(call GATE2_DLL,$(m)))
GATE2_HAVE_GAME := $(if $(filter-out $(wildcard $(GATE2_DLLS)),$(GATE2_DLLS)),,yes)

# gate2-check: the number of lifted functions `registry` must report for each module.  tier0
# lifted 1,886 of 1,926 and vstdlib 1,971 of 2,015 on 2026-10-09; the lifter only gains.
GATE2_MIN_tier0   ?= 1800
GATE2_MIN_vstdlib ?= 1900
GATE2_MAX_LIFTED  ?= 3000

.PHONY: gate2 gate2-build gate2-registry gate2-check

ifeq ($(GATE2_HAVE_GAME),yes)

# ---- lifting: when the lifted C is missing or older than the lifter or the DLL -----------------
GATE2_LIFTER_SRC := $(wildcard lifter/*.py lifter/insns/*.py) lifter/guest_patches.toml
define gate2_lift_rule
$$(GUEST_LIFTED_DIR)/$(1)/$(1).report.json: $$(call GATE2_DLL,$(1)) $$(GATE2_LIFTER_SRC)
	@mkdir -p $$(GATE2_OUT)
	@echo "  LIFTER $(1)  (log: $$(GATE2_OUT)/lift-$(1).log)"
	$$(RT_Q)$$(MAKE) --no-print-directory lift MODULE=$(1) > $$(GATE2_OUT)/lift-$(1).log 2>&1 \
	    || { tail -20 $$(GATE2_OUT)/lift-$(1).log; exit 1; }
	$$(RT_SETTLE)
endef
$(foreach m,$(GATE2_MODS),$(eval $(call gate2_lift_rule,$(m))))
$(foreach m,$(GATE2_MODS),$(eval $(call rt_lifted_rules,$(m),$(GUEST_LIFTED_DIR)/$(m))))
GATE2_LIFTED := $(foreach m,$(GATE2_MODS),$(RT_OUT)/lifted/$(m).o)

# ---- the driver's module table ---------------------------------------------------------------------
# gate2/modules.def holds the two `lifted` rows and nothing else.  No --shims directory is
# given: the driver has no shims of its own, so the generated shim table is the terminator
# row with a count of 0, and the imports of the two DLLs resolve against librt.a's own
# tables (runtime/modules.def, runtime/shims): a shim's stub, or a trap stub from the pre-scan.
$(GATE2_GEN)/.stamp: gate2/modules.def $(RT_TOOLS) runtime/tools/types.def $(GATE2_DLLS)
	@mkdir -p $(GATE2_GEN)
	$(PY) runtime/tools/gen_shims.py --types runtime/tools/types.def --modules gate2/modules.def \
	    --out $(GATE2_GEN) --prefix gate2 --table gate2_shim_table --module-table gate2_module_defs
	@touch $@
	$(RT_SETTLE)
$(GATE2_GEN)/gate2_table.c $(GATE2_GEN)/gate2_ids.h: $(GATE2_GEN)/.stamp ;

# ---- stand-ins for loader functions that are still stubs (gate2/peek.c explains) ---------------------
# $(call gate2_is_stub,<file>,<function>) is non-empty while the body of <function> in <file>
# is RT_CORE_UNIMPLEMENTED (looked for in the two lines after the function's first line).
gate2_is_stub = $(shell grep -A 2 '^void $(2)' $(1) 2>/dev/null | grep -o -m 1 RT_CORE_UNIMPLEMENTED)
GATE2_STANDINS := $(if $(call gate2_is_stub,runtime/core/loader_import.c,rt_ldr_import_prescan),-DGATE2_STANDIN_IMPORT_PRESCAN) \
                  $(if $(call gate2_is_stub,runtime/core/loader_tls.c,rt_ldr_tls_prescan),-DGATE2_STANDIN_TLS_PRESCAN)

GATE2_CFLAGS := $(RT_CFLAGS) -I$(GATE2_GEN) -Igate2 $(strip $(GATE2_STANDINS)) \
                -DGATE2_INSTALL_ROOT='"$(abspath game)"' -DGATE2_STATE_DIR='"$(abspath $(GATE2_OUT)/state)"'
GATE2_SRC    := $(sort $(wildcard gate2/*.c))
GATE2_OBJ    := $(patsubst gate2/%.c,$(GATE2_OUT)/obj/%.o,$(GATE2_SRC)) $(GATE2_OUT)/obj/gate2_table.o

# The flags are a dependency, so that a stand-in appears or goes when the runtime changes.
$(GATE2_OUT)/.flags: RT_FORCE
	@mkdir -p $(GATE2_OUT)
	@echo '$(GATE2_CFLAGS)' | cmp -s - $@ || echo '$(GATE2_CFLAGS)' > $@

$(GATE2_OUT)/obj/%.o: gate2/%.c $(GATE2_OUT)/.flags | $(GATE2_GEN)/.stamp $(RT_GEN)/.shims.stamp
	@mkdir -p $(dir $@)
	$(if $(V),,@echo "  CC    $<")
	$(RT_Q)$(RT_CC) $(GATE2_CFLAGS) -MMD -MP -c $< -o $@

$(GATE2_OUT)/obj/gate2_table.o: $(GATE2_GEN)/gate2_table.c $(GATE2_OUT)/.flags
	@mkdir -p $(dir $@)
	$(if $(V),,@echo "  CC    $<")
	$(RT_Q)$(RT_CC) $(GATE2_CFLAGS) -c $< -o $@

$(GATE2_BIN): $(GATE2_OBJ) $(GATE2_LIFTED) $(RT_OUT)/librt.a
	$(if $(V),,@echo "  LINK  $@$(if $(strip $(GATE2_STANDINS)),  (with stand-ins: $(strip $(GATE2_STANDINS))))")
	$(RT_Q)$(RT_CC) -o $@ $(GATE2_OBJ) $(GATE2_LIFTED) $(RT_OUT)/librt.a
	$(RT_SETTLE)

-include $(GATE2_OBJ:.o=.d)

gate2-build: $(GATE2_BIN)

gate2-registry: $(GATE2_BIN)
	$(GATE2_BIN) registry

# Each mode is run whatever the one before it did; the summary line says how each ended.
# Exit status 0 only when the load completes and no call fails or stops.
gate2: $(GATE2_BIN)
	@$(GATE2_BIN) registry; r=$$?; echo; \
	$(GATE2_BIN) load; l=$$?; echo; \
	$(GATE2_BIN) call > $(GATE2_OUT)/call.log; c=$$?; cat $(GATE2_OUT)/call.log; echo; \
	echo "gate2: registry $$( [ $$r = 0 ] && echo ok || echo "FAILED (status $$r)" ); load $$( [ $$l = 0 ] && echo ok || echo "did not complete (status $$l)" ); $$(tail -1 $(GATE2_OUT)/call.log)"; \
	[ $$r = 0 ] && [ $$l = 0 ] && [ $$c = 0 ]

gate2-check: $(GATE2_BIN)
	@$(GATE2_BIN) registry > $(GATE2_OUT)/registry.log; s=$$?; cat $(GATE2_OUT)/registry.log; \
	[ $$s = 0 ] || { echo "gate2-check: FAILED: gate2 registry ended with status $$s"; exit 1; }; \
	for want in tier0.dll:$(GATE2_MIN_tier0) vstdlib.dll:$(GATE2_MIN_vstdlib); do \
	    awk -v mod=$${want%:*} -v min=$${want#*:} -v max=$(GATE2_MAX_LIFTED) \
	        '$$1 == "module" && $$2 == mod && $$3 == "present" { for (i = 4; i <= NF; i++) if (split($$i, kv, "=") == 2 && kv[1] == "lifted" && kv[2] + 0 >= min && kv[2] + 0 <= max) ok = 1 } END { exit !ok }' \
	        $(GATE2_OUT)/registry.log \
	    || { echo "gate2-check: FAILED: $${want%:*} is not reported present with between $${want#*:} and $(GATE2_MAX_LIFTED) lifted functions"; exit 1; }; \
	done; \
	echo "gate2-check: ok"

# ---- a diagnostic driver in which the C of one guest instruction is replaced --------------------
#   make gate2-diag DIAG=<name> DIAG_RVA=<hex rva> DIAG_C='<C statements>' [DIAG_MODULE=tier0]
# links $(GATE2_OUT)-diag-<name>/gate2 from the driver's own objects, the runtime, and the lifted
# module with one file recompiled from a copy that gate2/diag_lifted.py edits (it says what `@@`
# is).  For experiments on guest code; works with GUEST_MEM=seqcst.  Everything lands in build/.
DIAG_MODULE   ?= tier0
GATE2_DIAG_OUT = $(GATE2_OUT)-diag-$(DIAG)
.PHONY: gate2-diag
gate2-diag: $(GATE2_BIN)
	@test -n "$(DIAG)" -a -n "$(DIAG_RVA)" || { echo "usage: make gate2-diag DIAG=<name> DIAG_RVA=<hex rva> DIAG_C='<C statements>' [DIAG_MODULE=tier0]"; exit 2; }
	@rm -rf $(GATE2_DIAG_OUT) && mkdir -p $(GATE2_DIAG_OUT)/obj
	@cp $(RT_OUT)/lifted/$(DIAG_MODULE)/*.o $(GATE2_DIAG_OUT)/obj/
	$(RT_Q)c=$$($(PY) gate2/diag_lifted.py --module $(DIAG_MODULE) --lifted $(GUEST_LIFTED_DIR)/$(DIAG_MODULE) --rva $(DIAG_RVA) --c '$(DIAG_C)' --out $(GATE2_DIAG_OUT)/src) || exit 1; \
	    $(RT_CC) $(RT_LIFT_CFLAGS) -I$(GUEST_LIFTED_DIR)/$(DIAG_MODULE) -c $$c -o $(GATE2_DIAG_OUT)/obj/$$(basename $$c .c).o
	$(RT_Q)ld -r -o $(GATE2_DIAG_OUT)/$(DIAG_MODULE).o $(GATE2_DIAG_OUT)/obj/*.o
	$(RT_Q)$(RT_CC) -o $(GATE2_DIAG_OUT)/gate2 $(GATE2_OBJ) $(filter-out $(RT_OUT)/lifted/$(DIAG_MODULE).o,$(GATE2_LIFTED)) $(GATE2_DIAG_OUT)/$(DIAG_MODULE).o $(RT_OUT)/librt.a
	@echo "  LINK  $(GATE2_DIAG_OUT)/gate2  ($(DIAG_MODULE)+0x$(DIAG_RVA) replaced)"

else

# A clone without the game: nothing can be lifted, generated or run.
gate2-check:
	@echo "gate2-check: SKIPPED: no $(firstword $(filter-out $(wildcard $(GATE2_DLLS)),$(GATE2_DLLS))) here (a checkout without the game's files); nothing was built or run"

gate2 gate2-build gate2-registry:
	@echo "$@: needs $(GATE2_DLLS), and $(firstword $(filter-out $(wildcard $(GATE2_DLLS)),$(GATE2_DLLS))) is not there"; exit 1

endif
