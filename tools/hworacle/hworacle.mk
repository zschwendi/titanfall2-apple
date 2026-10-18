# Real-CPU oracle (tools/hworacle/README.md).  Included by the root Makefile.
#
#   make hworacle-test         everything that needs no other machine: byte inspection, wire format,
#                              cache, client against a stand-in for ssh, offline mode
#   make hworacle-live         needs the x86 machine: known answers, sandbox, time limit, cache proof
#   make hworacle-status       is the machine there, which CPU, is the runner built
#   make hworacle-crosscheck   Unicorn against the hardware (needs lifted modules for the game DLLs;
#                              answers already in the cache are not asked again)
#   make hworacle-sweep        rcpss/rsqrtss on the Intel CPU against frecpe/frsqrte on this Mac

HWO := tools/hworacle

.PHONY: hworacle-test hworacle-live hworacle-status hworacle-crosscheck hworacle-sweep

build/hworacle/refuse_check: $(HWO)/runner.c
	@mkdir -p build/hworacle
	cc -O1 -DHWO_CHECK_TOOL -Wall -Wextra -Werror -o $@ $<

hworacle-test: build/hworacle/refuse_check
	$(PY) $(HWO)/tests/test_refusal.py
	$(PY) $(HWO)/tests/test_cache.py
	$(PY) $(HWO)/tests/test_client.py
	$(PY) $(HWO)/tests/test_offline.py

hworacle-live:
	$(PY) $(HWO)/tests/live.py

hworacle-status:
	$(PY) $(HWO)/client.py status

# Unicorn against the hardware.  HWO_FLAGS=--offline answers from the cache only.
hworacle-crosscheck:
	$(PY) $(HWO)/crosscheck.py insn --module tier0 $(HWO_FLAGS)
	$(PY) $(HWO)/crosscheck.py insn --module vstdlib $(HWO_FLAGS)
	$(PY) $(HWO)/crosscheck.py insn --module corpus_O1 --dll tests/corpus/build/corpus_O1.dll $(HWO_FLAGS)
	$(PY) $(HWO)/crosscheck.py insn --module corpus_O2 --dll tests/corpus/build/corpus_O2.dll $(HWO_FLAGS)
	$(PY) $(HWO)/crosscheck.py insn --module corpus_Os --dll tests/corpus/build/corpus_Os.dll $(HWO_FLAGS)
	$(PY) $(HWO)/crosscheck.py insn --module vectors --states 48 $(HWO_FLAGS)
	$(PY) $(HWO)/crosscheck.py insn --module extra --states 48 $(HWO_FLAGS)
	$(PY) $(HWO)/crosscheck.py float $(HWO_FLAGS)
	$(PY) $(HWO)/crosscheck.py cases $(HWO_FLAGS)
	$(PY) $(HWO)/crosscheck.py report

hworacle-sweep:
	$(PY) $(HWO)/sweep.py $(HWO_FLAGS)
