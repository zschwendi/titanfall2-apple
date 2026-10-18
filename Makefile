# Static recompiler: lifter and differential test harness.  See docs/DESIGN.md.
#
#   make test                      self-tests (no game files needed)
#   make lift MODULE=tier0         lift game/bin/x64_retail/<MODULE>.dll into lifted/<MODULE>/
#                                  (GUEST_PATCHES=off: without the guest patches of lifter/guest_patches.toml,
#                                  into build/lifted-unpatched/<MODULE>/; runtime/rt.mk says what that is for)
#   make reloc-lint                where every relocated field lies, and what looks like one without being one, by
#                                  kind, for the 20 boot-path modules (RELOC_MODULES="engine client" for others);
#                                  lifts in memory and writes nothing; fails if anything is of an unknown kind
#   make difftest MODULE=tier0     isolated function test, then the instruction test
#   make difftest-func MODULE=..   only the function test
#   make difftest-insn MODULE=..   only the instruction test
#   make family-check FAMILY=sse_mov   is this instruction family done?  (tools/family_check.py;
#                                  BASE=<branch the worktree was cut from>, FRESH=1 to ignore cached results)
#   make insn-index                build or refresh the instruction index of the game and corpus DLLs
#   make worklist                  what the lifter does not translate yet, from that index

PY     := .venv/bin/python
MODULE ?= tier0
SEEDS  ?= 12

FAMILY ?=
BASE   ?= wip/lifter-v0

# The 20 modules of the boot path (docs/handoff/round3-plan.md section 3; the list is the Gate 3 driver's).
RELOC_MODULES ?= tier0 vstdlib launcher rtech_game filesystem_stdio engine bink2w64 inputsystem vphysics \
                 amd_ags_x64 materialsystem_dx11 datacache studiorender vguimatsurface vgui2 localize mileswin64 \
                 binkawin64 client server

.PHONY: test lift reloc-lint difftest difftest-func difftest-insn clean family-check insn-index worklist

test:
	$(PY) tools/hworacle/seed.py install
	$(PY) tests/run_selftests.py
	$(PY) tests/guest_patches_selftest.py
	$(PY) tools/trace/selftest.py
	$(PY) tools/gamedata/selftest.py

# Exit status 3: the C was written, but the relocation lint found something of an unknown kind (lifter/relocs.py).
lift:
	$(PY) -m lifter.lift --module $(MODULE) $(if $(filter off,$(GUEST_PATCHES)),--no-patches)

reloc-lint:
	@st=0; for m in $(RELOC_MODULES); do $(PY) -m lifter.lift --module $$m --lint-only || st=1; done; exit $$st

# The Direct3D call-trace tools (tools/trace/d3d): the parser and the counters on made-up call lines.
.PHONY: d3dtrace-test
test: d3dtrace-test
d3dtrace-test:
	$(PY) tools/trace/d3d/d3dtrace.py selftest
	$(PY) tools/trace/d3d/d3dscript.py selftest
	$(PY) tools/trace/d3d/tracefile.py selftest
	$(PY) tools/trace/d3d/frame_compare.py selftest

# The tool that writes an input script from the recording (tools/trace/input_script.py), on made-up lines.
.PHONY: inputscript-test
test: inputscript-test
inputscript-test:
	$(PY) tools/trace/input_script.py selftest

difftest: difftest-func difftest-insn

# A non-zero exit means at least one function or instruction disagrees with the oracle (1), or
# that lifted/$(MODULE) was not produced by the current lifter from the current DLL (2): the
# tests refuse to run on stale output, so run 'make lift' again after any change to lifter/.
difftest-func:
	$(PY) harness/difftest.py func --module $(MODULE) --seeds $(SEEDS)

difftest-insn:
	$(PY) harness/difftest.py insn --module $(MODULE)

# The definition of done for an instruction family (docs/DESIGN.md, "Adding an instruction family").
# Exit status 0 = done, 1 = not done, 2 = could not run.
family-check:
	@$(PY) tools/family_check.py --family "$(FAMILY)" --base "$(BASE)" $(if $(FRESH),--fresh) $(if $(KEEP),--keep) $(if $(JOBS),--jobs $(JOBS))

insn-index:
	$(PY) tools/census/insn_index.py

worklist:
	$(PY) tools/census/insn_index.py --worklist

clean:
	rm -rf harness/out lifted

# Native runtime (docs/RUNTIME.md).  Its rules live in runtime/rt.mk so runtime work never edits this file.
-include runtime/rt.mk
-include runtime/replay.mk

# The Gate 2 driver: tier0 and vstdlib loaded in the runtime (gate2/gate2.mk; needs runtime/rt.mk).
#   make gate2-build | gate2-registry | gate2 | gate2-check
-include gate2/gate2.mk

# The Gate 3 driver: the 20 boot-path DLLs and the pak loader's ui(11).dll (21 lifted modules,
# gate3/modules.def) in one program, their lifted code from a shared
# cache (gate3/gate3.mk; needs runtime/rt.mk).
#   make gate3-build | gate3-registry | gate3-load | gate3-check | gate3-keys | gate3-stats | gate3-prune
-include gate3/gate3.mk

# Real-CPU oracle (tools/hworacle/README.md).
-include tools/hworacle/hworacle.mk

# Offscreen Metal measurements of this GPU (tools/metalbench/README.md).
#   make metalbench
-include tools/metalbench/metalbench.mk

# The Windows reference program: the driver's calls against the real DLLs (tools/ref/ref.mk).
#   make ref-build | ref-libc-test | ref-check
-include tools/ref/ref.mk

# Speed of lifted code against native (bench/README.md).
-include bench/bench.mk

# The signed app bundle around one of the runtime's programs (app/app.mk; its header says who
# may run it).  Its three targets are named app-*, its variables APP_*, and nothing builds them
# unless asked.
#   make app-sign APP_BIN=<program> | app-run | app-check
-include app/app.mk

# The function test on all the modules of gate3/modules.def (the 20 of the boot path and ui11), in chunks, detached and resumable
# (tools/functest/functest.py; docs/handoff/harness-func-big.md).  FUNCTEST_JOBS: processes at once.
#   make functest-start | functest-status | functest-stop | functest-failures | functest-report | functest-plan
FUNCTEST_JOBS ?= 4
.PHONY: functest-start functest-status functest-stop functest-failures functest-report functest-plan
functest-start:
	@$(PY) tools/functest/functest.py start --jobs $(FUNCTEST_JOBS)
functest-status functest-stop functest-failures functest-report functest-plan:
	@$(PY) tools/functest/functest.py $(patsubst functest-%,%,$@)
