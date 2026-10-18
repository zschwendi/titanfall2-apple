# The Windows reference program: our driver's calls made to the real tier0.dll and vstdlib.dll
# (tools/ref/refcalls.c; docs/handoff/ref-proton.md).  Included by the root Makefile.
#
#   make ref-build       build/ref/refcalls.exe (Windows x64) and, beside it, a copy of
#                        tools/ref/run_ref.sh: the two files that go to the other machine
#   make ref-libc-test   tools/ref/ref_libc.c against this Mac's C library (runs natively)
#   make ref-check       both, the script's syntax and a dry run of it with a stand-in Wine
#                        (tools/ref/run_ref_dryrun.sh), compare.py's self-test, and that the
#                        program imports from kernel32.dll only (tools/ref/check_exe.py)
#
# Needs Apple clang (it makes COFF objects) and lld-link (Homebrew: brew install lld), as
# tests/corpus/build.sh does; no Windows SDK, no mingw.  The program is compiled from the
# driver's own call tables (gate2/calls*.c) against tools/ref/compat, which stands in for the
# runtime's headers; it contains no game code and reads no game file when it is built.
# NOTHING HERE RUNS THE PROGRAM: this Mac runs no Windows executable.
#
# -Wmicrosoft-enum-value (an error here): for this target an enumerator is an int, as Microsoft's
# compiler has it, and a value that does not fit is cut to 32 bits without a word from -Wall
# -Wextra, while the driver's build for the Mac keeps all 64.  Rank 16's time limit reached the
# real DLL as -1824522369 that way (docs/handoff/ref-rank16.md).  A table constant above
# 2^31 - 1 is a #define or a typed constant, never an enumerator.
REF_OUT    := build/ref
REF_CC     ?= clang
REF_LLD    ?= $(shell command -v lld-link || echo /opt/homebrew/opt/lld/bin/lld-link)
REF_CFLAGS := --target=x86_64-pc-windows-msvc -std=gnu17 -O1 -ffreestanding -fno-builtin -fno-stack-protector \
              -fno-math-errno -fno-strict-aliasing -ffp-contract=off -mno-avx -msse2 -funwind-tables \
              -Wall -Wextra -Werror -Wno-unused-parameter -Wmicrosoft-enum-value \
              -Itools/ref/compat -Itools/ref -Iruntime/include -Igate2
REF_SRC    := tools/ref/refcalls.c tools/ref/ref_libc.c gate2/calls.c gate2/calls_more.c gate2/calls_wide.c
REF_OBJ    := $(patsubst %.c,$(REF_OUT)/obj/%.obj,$(notdir $(REF_SRC)))
REF_DEPS   := $(wildcard tools/ref/*.h tools/ref/compat/*.h tools/ref/compat/rt/*.h tools/ref/compat/sys/*.h gate2/*.h gate2/*.inc) tools/ref/ref.mk

.PHONY: ref-build ref-libc-test ref-check

ref-build: $(REF_OUT)/refcalls.exe $(REF_OUT)/run_ref.sh
	@ls -l $(REF_OUT)/refcalls.exe $(REF_OUT)/run_ref.sh

vpath %.c tools/ref gate2
$(REF_OUT)/obj/%.obj: %.c $(REF_DEPS)
	@mkdir -p $(REF_OUT)/obj
	@echo "  CC(win64)  $<"
	@$(REF_CC) $(REF_CFLAGS) -c $< -o $@

$(REF_OUT)/kernel32.lib: tools/ref/kernel32.def
	@mkdir -p $(REF_OUT)
	@$(REF_LLD) /lib /machine:x64 /def:$< /out:$@

# /Brepro: no time stamp, so the same sources give the same bytes.  4 MB of stack for every thread.
$(REF_OUT)/refcalls.exe: $(REF_OBJ) $(REF_OUT)/kernel32.lib
	@echo "  LINK(win64)  $@"
	@$(REF_LLD) /out:$@ /subsystem:console /entry:mainCRTStartup /nodefaultlib /machine:x64 /dynamicbase /Brepro \
	    /stack:0x400000,0x10000 /map:$(REF_OUT)/refcalls.map $(REF_OBJ) $(REF_OUT)/kernel32.lib

$(REF_OUT)/run_ref.sh: tools/ref/run_ref.sh
	@mkdir -p $(REF_OUT)
	@cp $< $@ && chmod +x $@

$(REF_OUT)/libc_test: tools/ref/libc_test.c tools/ref/ref_libc.c
	@mkdir -p $(REF_OUT)
	@$(REF_CC) -O1 -Wall -Wextra -Wno-format -fno-builtin -fno-math-errno -DREF_LIBC_TEST -c tools/ref/ref_libc.c -o $(REF_OUT)/ref_libc_native.o
	@$(REF_CC) -O1 -Wall -Wno-format -Wno-format-security tools/ref/libc_test.c $(REF_OUT)/ref_libc_native.o -o $@

ref-libc-test: $(REF_OUT)/libc_test
	@$(REF_OUT)/libc_test

ref-check: ref-build ref-libc-test
	@bash -n tools/ref/run_ref.sh && echo "run_ref.sh: syntax ok"
	@if command -v shellcheck > /dev/null; then shellcheck tools/ref/run_ref.sh && echo "run_ref.sh: shellcheck ok"; else echo "run_ref.sh: shellcheck is not installed"; fi
	@bash tools/ref/run_ref_dryrun.sh
	@$(PY) tools/ref/compare.py --self-test
	@$(PY) tools/ref/check_exe.py $(REF_OUT)/refcalls.exe
