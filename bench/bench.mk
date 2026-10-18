# Speed and correctness of lifted code against the same C built natively (bench/README.md).
#
#   make bench-smoke    build both sides, compare every described function, short timing pass
#   make bench          the full timing run (writes bench/build/results.json)
#   make bench-gap      the builds behind the gap analysis in docs/findings/25-lifted-speed.md
#   make bench-mutants  break the lifted C on purpose: does the comparison notice?
#   make bench-tso      the timing inside the entitled probe app, x86 memory ordering off and on
#   make bench-clean
#
# Included from the root Makefile; every path is relative to the repository root.

BENCH_PY    ?= .venv/bin/python
BB          := bench/build
BENCH_FILES := abi atomics calls cpp2 int_arith intrare mem_str structs switches virt
BENCH_CC    ?= clang
BENCH_CXX   ?= clang++
# The lifted C is compiled the way harness/difftest.py compiles it, at the level under test.
LIFT_OPT    ?= -O2
LIFT_FLAGS  := -g0 -fno-strict-aliasing -fwrapv -Iharness
# The native side: the corpus's own flags (tests/corpus/build.sh) minus the Windows target.
NATIVE_FLAGS := -O2 -ffreestanding -fno-stack-protector -fno-exceptions -fno-math-errno \
                -fvisibility=hidden -Wall -Wno-unused-function -include bench/native/prefix.h -Ibench/native
DRIVER_FLAGS := -O2 -g -Wall -Wextra -fno-strict-aliasing -Iharness -Ibench

BENCH_TABLES  := $(foreach f,$(BENCH_FILES),bench/functions/$(f).tbl)
BENCH_DLLS    := $(foreach f,$(BENCH_FILES),tests/corpus/build/one_$(f)_O2.dll)
BENCH_LIFTED  := $(foreach f,$(BENCH_FILES),$(BB)/lifted/one_$(f).stamp)
BENCH_OBJS    := $(foreach f,$(BENCH_FILES),$(BB)/obj/one_$(f).o)
BENCH_DYLIBS  := $(foreach f,$(BENCH_FILES),$(BB)/native/$(f).dylib)

.PHONY: bench bench-smoke bench-build bench-clean bench-gap bench-mutants bench-tso
.SECONDARY:

# The corpus DLLs.  build.sh rebuilds everything, so one stamp stands for all of them.
$(BB)/corpus.stamp: $(wildcard tests/corpus/*.c tests/corpus/*.cpp tests/corpus/*.h) tests/corpus/build.sh
	@mkdir -p $(BB)
	sh tests/corpus/build.sh > $(BB)/corpus-build.log 2>&1 || { cat $(BB)/corpus-build.log; exit 1; }
	@touch $@

# Lift one module.  Only the lifter's command line and its output files are used.
$(BB)/lifted/one_%.stamp: $(BB)/corpus.stamp $(wildcard lifter/*.py)
	@mkdir -p $(BB)/lifted
	$(BENCH_PY) -m lifter.lift --module one_$* --dll tests/corpus/build/one_$*_O2.dll --out $(BB)/lifted/one_$* > $(BB)/lifted/one_$*.log 2>&1 \
	    || { cat $(BB)/lifted/one_$*.log; exit 1; }
	@touch $@

# The lifted C of one module, however many files the lifter split it into, as one object.
$(BB)/obj/one_%.o: $(BB)/lifted/one_%.stamp $(wildcard harness/*.h)
	@mkdir -p $(BB)/obj/one_$*
	@rm -f $(BB)/obj/one_$*/*.o
	for c in $(BB)/lifted/one_$*/one_$*_*.c; do \
	    $(BENCH_CC) $(LIFT_OPT) $(LIFT_FLAGS) -I$(BB)/lifted/one_$* -c $$c -o $(BB)/obj/one_$*/$$(basename $$c .c).o || exit 1; \
	done
	ld -r -o $@ $(BB)/obj/one_$*/*.o

# The native side: the same source for arm64, with mem_str.c beside it as in the DLL.
$(BB)/native/mem_str.o: tests/corpus/mem_str.c bench/native/prefix.h
	@mkdir -p $(BB)/native
	$(BENCH_CC) $(NATIVE_FLAGS) -c $< -o $@
$(BB)/native/%.o: tests/corpus/%.c bench/native/prefix.h bench/native/emmintrin.h
	@mkdir -p $(BB)/native
	$(BENCH_CC) $(NATIVE_FLAGS) -c $< -o $@
$(BB)/native/%.o: tests/corpus/%.cpp bench/native/prefix.h
	@mkdir -p $(BB)/native
	$(BENCH_CXX) $(NATIVE_FLAGS) -fno-rtti -c $< -o $@
# intrare.c is x86 inline assembly from top to bottom; its native side is a C reference.
$(BB)/native/intrare.o: bench/native/intrare_ref.c bench/native/prefix.h
	@mkdir -p $(BB)/native
	$(BENCH_CC) $(NATIVE_FLAGS) -c $< -o $@
$(BB)/native/mem_str.dylib: $(BB)/native/mem_str.o
	$(BENCH_CXX) -dynamiclib -o $@ $^
$(BB)/native/%.dylib: $(BB)/native/%.o $(BB)/native/mem_str.o
	$(BENCH_CXX) -dynamiclib -o $@ $^

$(BB)/gen/cases.c: $(BENCH_TABLES) bench/gen.py $(BENCH_LIFTED)
	$(BENCH_PY) bench/gen.py --table $(BENCH_TABLES) --lifted $(BB)/lifted --dll-dir tests/corpus/build \
	    --native-dir $(BB)/native --out $@

$(BB)/driver: bench/driver.c bench/bench.h $(BB)/gen/cases.c $(BENCH_OBJS) $(wildcard harness/*.h)
	$(BENCH_CC) $(DRIVER_FLAGS) $(DRIVER_DEFS) bench/driver.c $(BB)/gen/cases.c $(BENCH_OBJS) -o $@

bench-build:
	@$(MAKE) --no-print-directory -j8 $(BB)/driver $(BENCH_DYLIBS)

bench-smoke: bench-build
	$(BB)/driver --known bench/known_mismatches.tbl --cases 3000 --rounds 5 --max-rounds 20 --target-ms 0.3 --json $(BB)/smoke.json

bench: bench-build
	$(BB)/driver --known bench/known_mismatches.tbl --cases 20000 --rounds 30 --max-rounds 120 --target-ms 1 --json $(BB)/results.json

# Variant builds of the lifted side (optimisation level, restrict, dead flags, one function per
# translation unit, direct-mapped dispatch, the hand-edited functions), each compared and timed.
bench-gap: bench-build
	$(BENCH_PY) bench/variants.py gap
	$(BENCH_PY) bench/report.py variants $(foreach v,O2 O1 O3 split restrict deadflags deadflags-O3 direct deadflags-direct,$(BB)/v/$(v)/results.json)

# Deliberately wrong translations: does the comparison notice each of them?
bench-mutants: bench-build
	$(BENCH_PY) bench/variants.py mutants

# The benchmark inside the entitled probe app, with x86 memory ordering off and on.  Uses the
# existing provisioning only (see bench/tso.sh for the limits); fails if that is not enough.
bench-tso: bench-build
	sh bench/tso.sh

bench-clean:
	rm -rf $(BB)
