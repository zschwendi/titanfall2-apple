#!/bin/sh
# The driver's guard for EA's SDK runs BEFORE ANY GAME CODE: the test (finding 6 of
# docs/reviews/night-2026-10-11-code-review.md; unit fix-review-night).
#
#   check_origin_fence.sh <gate3 binary> <scratch directory>
#       Starts `gate3 run` with GATE3_TEST_ORIGIN_CHECK_FAILS=1, which makes the guard's verdict
#       "bad" and changes nothing else (no row is added to any table, nothing is answered).
#       The run must be refused, and its log must show that no module of the game was built
#       in memory before the refusal: no line "<module>: image built at", which the loader
#       writes for every lifted module before it runs that module's entry point.  Before the
#       fix the guard ran after tier0.dll, vstdlib.dll and launcher.dll were loaded (their
#       entry points run) and after the guest call for the language: three such lines.
#   check_origin_fence.sh --log <log of a run>
#       For the log of an ordinary run: the guard's own line is there, once, and stands before
#       the first "image built at" line.
#
# Reads the run's log and nothing of the game.  Exit 0 = held, 1 = not held, 2 = usage.
set -u
if [ "${1:-}" = "--log" ]; then
    log=${2:?usage: check_origin_fence.sh --log <log>}
    [ -f "$log" ] || { echo "origin-fence: no log $log"; exit 2; }
    rule=$(grep -n 'gate3 run: stand-in: THE RULE ABOUT EA.S SDK HOLDS' "$log" | cut -d: -f1)
    built=$(grep -n ': image built at ' "$log" | head -1 | cut -d: -f1)
    n=$(printf '%s\n' "$rule" | grep -c .)
    [ "$n" = 1 ] || { echo "origin-fence: FAILED: the guard's line is in $log $n time(s), not once"; exit 1; }
    [ -n "$built" ] || { echo "origin-fence: FAILED: $log has no 'image built at' line: not the log of a run that loaded a module"; exit 1; }
    [ "$rule" -lt "$built" ] || { echo "origin-fence: FAILED: the guard's line is line $rule, after the first module was built (line $built)"; exit 1; }
    echo "origin-fence: ok: the guard's line is line $rule of $log; the first module is built at line $built"
    exit 0
fi
bin=${1:?usage: check_origin_fence.sh <gate3 binary> <scratch directory>}
dir=${2:?usage: check_origin_fence.sh <gate3 binary> <scratch directory>}
mkdir -p "$dir" || exit 2
rm -f "$dir/fence.log" "$dir/fence.stderr" "$dir/fence.out"
GATE3_TEST_ORIGIN_CHECK_FAILS=1 GATE3_RUN_OUT="$dir/fence" GATE3_TIMEOUT=60 RT_LOG=all:info "$bin" run > "$dir/fence.out" 2>&1
status=$?
[ "$status" = 1 ] || { echo "origin-fence: FAILED: a run whose guard says 'bad' ended with the driver's status $status, not 1"; exit 1; }
grep -q 'not started (every import of EA.s SDK but the posted stand-ins must halt' "$dir/fence.stderr" "$dir/fence.out" 2>/dev/null ||
    { echo "origin-fence: FAILED: the refusal's text is not in $dir/fence.stderr"; exit 1; }
[ -f "$dir/fence.log" ] || { echo "origin-fence: FAILED: the refused run left no log $dir/fence.log"; exit 1; }
grep -q 'runtime started: ' "$dir/fence.log" || { echo "origin-fence: FAILED: $dir/fence.log does not show the runtime's start: the log is not whole"; exit 1; }
built=$(grep -c ': image built at ' "$dir/fence.log")
answered=$(grep -c 'stand-in [0-9]* answered' "$dir/fence.log")
called=$(grep -c 'calling LauncherMain' "$dir/fence.log")
if [ "$built" != 0 ] || [ "$answered" != 0 ] || [ "$called" != 0 ]; then
    echo "origin-fence: FAILED: the guard refused the run AFTER game code: $built module(s) built in memory (each one's entry point runs when it is loaded), $answered answer(s) of a stand-in, LauncherMain called $called time(s)"
    grep ': image built at ' "$dir/fence.log" | cut -c1-100 | sed 's/^/    /'
    exit 1
fi
echo "origin-fence: ok: a run whose guard says 'bad' is refused before any module of the game is built in memory (0 'image built' lines, 0 answers of a stand-in, LauncherMain not called)"
