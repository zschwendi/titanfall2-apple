# The Direct3D replay player: recorded calls of the real game played through the do-nothing device
# (runtime/tools/d3dreplay/replay.h says how; docs/handoff/d3d-replay.md is the hand-off).
#
#   make d3d-replay-build   build/runtime/d3d-replay
#   make d3d-replay         the whole recording, every stop and disagreement in one table
#                           (build/trace/replay-table.md), held against the committed rows of
#                           docs/findings/41-d3d-replay.tsv: fails if a row is new or grew.
#                           Skipped, exit 0, where build/trace/script/ has no script files
#                           (tools/trace/d3d/convert.sh makes them from the recording)
#   make d3d-replay-first   the recording up to the first stop or disagreement
#   make d3d-replay-rows    as d3d-replay, and rewrite docs/findings/41-d3d-replay.tsv from this run
#   make d3d-replay-back    the same recording with the RECORDING back end behind the device (rt/d3dback.h):
#                           the table as build/trace/replay-back-table.md, held against the same committed
#                           rows, and per method the front's count and the back end's
#                           (build/trace/replay-back-counts.md); fails on a row that is new or grew and on a
#                           method whose counts differ.  "row not done" lists what tools/swarm/back_rows.py
#                           still has to do
#   RP_ARGS="--until 9000"  further options of the player (the head of runtime/tools/d3dreplay/main.c)
#   RP_DATA=DIR             with d3d-replay: give the device the recorded bytes of DIR where it gets zeros
#                           otherwise (runtime/tools/d3dreplay/data.h; DIR is build/trace/data of the main
#                           checkout, made by tools/trace/d3d/tracefile.py extract).  The table is then
#                           build/trace/replay-table-data.md, held against the same committed rows
#
# The regression test of the player itself is runtime/tests/core/d3d_replay.c (make rt-test), on
# the made-up script runtime/tools/d3dreplay/madeup.d3ds.

RP_BIN     := $(RT_OUT)/d3d-replay
RP_OBJ     := $(RT_OUT)/obj/runtime/tools/d3dreplay/main.o
RP_LAYOUT  := $(RT_GEN)/replay_layouts.h
RP_HEADERS := runtime/include/rt/win/base.h runtime/include/rt/win/dxgi.h runtime/include/rt/win/d3d11.h runtime/include/rt/win/d3d11view.h
RP_SCRIPTS ?= $(sort $(wildcard build/trace/script/mission1-*.d3ds.gz))
RP_ROWS    := docs/findings/41-d3d-replay.tsv
RP_DATA    ?=
RP_SUFFIX  := $(if $(RP_DATA),-data)

.PHONY: d3d-replay d3d-replay-build d3d-replay-first d3d-replay-rows d3d-replay-back dxback-smoke

$(RP_LAYOUT): $(RP_HEADERS) runtime/tools/gen_replay.py
	@mkdir -p $(RT_GEN)
	$(PY) runtime/tools/gen_replay.py --selftest
	$(PY) runtime/tools/gen_replay.py --out $(RT_GEN) $(RP_HEADERS)
	@touch $@

$(RP_OBJ): runtime/tools/d3dreplay/main.c runtime/tools/d3dreplay/replay.h runtime/tools/d3dreplay/data.h $(RP_LAYOUT) | $(RT_GEN)/.shims.stamp $(RT_GEN)/.com.stamp
	@mkdir -p $(dir $@)
	$(if $(V),,@echo "  CC    $<")
	$(RT_Q)$(RT_CC) $(RT_CFLAGS) -MMD -MP -c $< -o $@

# The DXMT back end (runtime/dxback/, unit dxback-skeleton): built and linked only where DXMT's Direct3D headers are
# (third_party/dxmt, a git-ignored clone).  Without them nothing here changes, and the player has no DXMT back end.
RP_DXMT_HDR := third_party/dxmt/include/native/directx/d3d11.h
RP_DXMT_DIR ?= $(CURDIR)/build/dxmt/Frameworks
ifneq ($(wildcard $(RP_DXMT_HDR)),)
DXB_INC  := -I third_party/dxmt/include/native/windows -I third_party/dxmt/include/native/directx -I third_party/dxmt/include/native -Iruntime/include -I$(RT_GEN)
DXB_SRCS := $(wildcard runtime/dxback/*.cpp)
DXB_OBJS := $(patsubst runtime/dxback/%.cpp,$(RT_OUT)/obj/runtime/dxback/%.o,$(DXB_SRCS))
DXB_LIBS := -lc++

$(RT_OUT)/obj/runtime/dxback/%.o: runtime/dxback/%.cpp runtime/include/rt/d3dback.h
	@mkdir -p $(dir $@)
	$(if $(V),,@echo "  CXX   $<")
	$(RT_Q)$(RT_CXX) -std=c++20 -w $(DXB_INC) -MMD -MP -c $< -o $@

-include $(DXB_OBJS:.o=.d)
endif

$(RP_BIN): $(RP_OBJ) $(DXB_OBJS) $(RT_OUT)/librt.a
	$(if $(V),,@echo "  LINK  $@")
	$(RT_Q)$(RT_CC) $(RT_SAN_FLAGS) -o $@ $(RP_OBJ) $(DXB_OBJS) $(RT_OUT)/librt.a $(DXB_LIBS) $(RT_FRAMEWORKS)

-include $(RP_OBJ:.o=.d)

# The regression test includes the engine: it needs the generated layouts, and the sample's path.
$(RT_OUT)/obj/runtime/tests/core/d3d_replay.o: RT_CFLAGS += -DRP_SAMPLE='"$(abspath runtime/tools/d3dreplay/madeup.d3ds)"'
$(RT_OUT)/obj/runtime/tests/core/d3d_replay.o: | $(RP_LAYOUT)
$(RT_OUT)/obj/runtime/tests/core/d3d_replay_data.o: | $(RP_LAYOUT)

d3d-replay-build: $(RP_BIN)

d3d-replay d3d-replay-rows: $(RP_BIN)
	@if [ -z "$(RP_SCRIPTS)" ]; then \
	    echo "$@: SKIPPED: no script files under build/trace/script (tools/trace/d3d/convert.sh makes them from the recording)"; \
	else \
	    mkdir -p build/trace; \
	    nice $(RP_BIN) --all --table build/trace/replay-table$(RP_SUFFIX).md --tsv build/trace/replay-rows$(RP_SUFFIX).tsv \
	        $(if $(filter d3d-replay,$@),$(if $(wildcard $(RP_ROWS)),--baseline $(RP_ROWS))) $(if $(RP_DATA),--data $(RP_DATA)) $(RP_ARGS) $(RP_SCRIPTS); s=$$?; \
	    $(if $(filter d3d-replay-rows,$@),[ $$s -le 1 ] && cp build/trace/replay-rows$(RP_SUFFIX).tsv $(RP_ROWS) && s=0;) \
	    echo "$@: the table is build/trace/replay-table$(RP_SUFFIX).md"; exit $$s; \
	fi

d3d-replay-first: $(RP_BIN)
	@if [ -z "$(RP_SCRIPTS)" ]; then \
	    echo "$@: SKIPPED: no script files under build/trace/script (tools/trace/d3d/convert.sh makes them from the recording)"; \
	else nice $(RP_BIN) --first $(RP_ARGS) $(RP_SCRIPTS); fi

d3d-replay-back: $(RP_BIN)
	@if [ -z "$(RP_SCRIPTS)" ]; then \
	    echo "$@: SKIPPED: no script files under build/trace/script (tools/trace/d3d/convert.sh makes them from the recording)"; \
	else \
	    nice $(RP_BIN) --all --backend record --table build/trace/replay-back-table.md --tsv build/trace/replay-back-rows.tsv \
	        --back-table build/trace/replay-back-counts.md $(if $(wildcard $(RP_ROWS)),--baseline $(RP_ROWS)) $(RP_ARGS) $(RP_SCRIPTS); s=$$?; \
	    echo "$@: the table is build/trace/replay-back-table.md, the counts are build/trace/replay-back-counts.md"; exit $$s; \
	fi

# The proof of the DXMT back end (docs/handoff/... dxback-skeleton brief): the player on a script with a device, one
# buffer and the releases, with RT_D3D_BACKEND=dxmt; then a script that calls an entry not built yet, which must stop
# by name.  Needs third_party/dxmt and the three libraries in RT_DXMT_DIR (the assert build of DXMT).  Every run has
# a hidden host window (RT_HOST_WINDOW=appkit-hidden: nothing is shown).  The swap-chain scripts (unit
# dxback-swapchain) write a shot of their first Present: the plain one must show DXMT's clear colour, the sRGB one
# is only reported.  The stop script runs with RT_DXBACK_UNBUILT=SetMaximumFrameLatency: that entry is left unbuilt
# on purpose, so the script stops there by name.
dxback-smoke: $(RP_BIN)
	@if [ -z "$(DXB_OBJS)" ]; then echo "$@: FAIL: the DXMT headers are not there ($(RP_DXMT_HDR))"; exit 1; fi
	@mkdir -p build
	@n=0; for f in runtime/tools/d3dreplay/dxback-smoke*.d3ds; do \
	    b=$$(basename $$f .d3ds); out=build/$$b.out; n=$$((n+1)); \
	    case $$f in \
	    *-stop.d3ds) \
	        name=$$(sed -n 's/.*::\([A-Za-z0-9]*\) .*/\1/p' $$f | tail -1); \
	        RT_HOST_WINDOW=appkit-hidden RT_DXBACK_UNBUILT=SetMaximumFrameLatency RT_D3D_BACKEND=dxmt RT_DXMT_DIR=$(RP_DXMT_DIR) $(RP_BIN) $$f > $$out 2>&1; \
	        if ! grep -Fq "dxback: $$name is not built yet" $$out; then tail -8 $$out; echo "$@: FAIL: $$f did not stop by name ($$name)"; exit 1; fi; \
	        echo "$$b: stopped by name at $$name (the last method of the script)";; \
	    *swapchain*) \
	        shotdir=build/shots; if [ $$b = dxback-smoke-swapchain-srgb ]; then shotdir=build/shots-srgb; fi; \
	        RT_HOST_WINDOW=appkit-hidden RT_D3D_SHOT=0 RT_D3D_SHOT_DIR=$$shotdir RT_D3D_BACKEND=dxmt RT_DXMT_DIR=$(RP_DXMT_DIR) $(RP_BIN) $$f > $$out 2>&1; s=$$?; \
	        if [ $$s -ne 0 ]; then tail -8 $$out; echo "$@: FAIL: $$f: the player exited $$s"; exit 1; fi; \
	        if grep -Fq "is not built yet" $$out; then tail -8 $$out; echo "$@: FAIL: $$f stopped at an entry not built yet"; exit 1; fi; \
	        if ! grep -q 'error=0 other=' $$out; then tail -8 $$out; echo "$@: FAIL: $$f: DXMT wrote error lines"; exit 1; fi; \
	        if [ $$b = dxback-smoke-swapchain ]; then \
	            $(PY) tools/trace/d3d/frame_compare.py stats $$shotdir/present-0.rtshot --clear 255,128,64 --tol 1 > $$out.stats 2>&1; \
	            if ! grep -q '"width": 256' $$out.stats || ! grep -q '"height": 144' $$out.stats || ! grep -q '"nonclear_share": 0.0' $$out.stats; then \
	                cat $$out.stats; echo "$@: FAIL: $$f: the shot is not the clear colour (255,128,64) at 256 by 144"; exit 1; fi; \
	            echo "$$b: exit 0, shot $$shotdir/present-0.rtshot: $$(tr -d '\n' < $$out.stats | cut -c1-200)"; \
	        else \
	            $(PY) tools/trace/d3d/frame_compare.py stats $$shotdir/present-0.rtshot > $$out.stats 2>&1; \
	            echo "$$b: exit 0, shot $$shotdir/present-0.rtshot, not a check: $$(tr -d '\n' < $$out.stats | cut -c1-200)"; \
	        fi;; \
	    *) \
	        RT_HOST_WINDOW=appkit-hidden RT_D3D_BACKEND=dxmt RT_DXMT_DIR=$(RP_DXMT_DIR) $(RP_BIN) $$f > $$out 2>&1; s=$$?; \
	        if [ $$s -ne 0 ]; then tail -8 $$out; echo "$@: FAIL: $$f: the player exited $$s"; exit 1; fi; \
	        if grep -Fq "is not built yet" $$out; then tail -8 $$out; echo "$@: FAIL: $$f stopped at an entry not built yet"; exit 1; fi; \
	        if ! grep -q '^dxback: back end dxmt: DXMT log lines by level:' $$out; then tail -8 $$out; echo "$@: FAIL: $$f: no line says the back end is dxmt with its counts"; exit 1; fi; \
	        if ! grep -q 'error=0 other=' $$out; then tail -8 $$out; echo "$@: FAIL: $$f: DXMT wrote error lines"; exit 1; fi; \
	        echo "$$b: exit 0, no stop, no error lines: $$(tail -1 $$out)";; \
	    esac; \
	done; \
	if [ $$n -eq 0 ]; then echo "$@: FAIL: no runtime/tools/d3dreplay/dxback-smoke*.d3ds"; exit 1; fi; \
	echo "$@: PASS: $$n scripts: the smoke ones went through DXMT, the stop ones stopped by name"
