#!/usr/bin/env bash
# A dry run of run_ref.sh's own logic on this Mac (`make ref-check`): no Wine, no Windows
# program, no game file.  It runs the script in a temporary directory against a made-up
# install directory and a made-up "Proton - Experimental" whose `wine64` is a shell script
# that makes an empty prefix and prints canned lines in refcalls.exe's format, with small
# stand-ins for the GNU commands this Mac lacks (realpath -e/-m, stat -c, timeout) and a
# `uname` that says Linux.  It checks the refusals, the three result files, that the prefix
# and work directory are gone afterwards, and that a run over the leftovers of an
# interrupted one succeeds.  What it cannot check is everything Wine does.
set -u
HERE=$(cd "$(dirname "$0")" && pwd -P)
T=$(mktemp -d "${TMPDIR:-/tmp}/run_ref_dryrun.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
fail() { echo "run_ref_dryrun: FAILED: $*"; exit 1; }

mkdir -p "$T/bin" "$T/home" "$T/lib/steamapps/common/Titanfall2/bin/x64_retail" "$T/lib/steamapps/common/Proton - Experimental/files/bin" "$T/tool"
printf 'not a real DLL: tier0\n' > "$T/lib/steamapps/common/Titanfall2/bin/x64_retail/tier0.dll"
printf 'not a real DLL: vstdlib\n' > "$T/lib/steamapps/common/Titanfall2/bin/x64_retail/vstdlib.dll"
echo "dryrun-proton-1" > "$T/lib/steamapps/common/Proton - Experimental/version"
cp "$HERE/run_ref.sh" "$T/tool/run_ref.sh"
printf 'not a real program\n' > "$T/tool/refcalls.exe"

cat > "$T/bin/uname" <<'SH'
#!/bin/sh
case "${1:-}" in -s) echo Linux ;; *) echo "Linux 0.0-dryrun x86_64" ;; esac
SH
cat > "$T/bin/realpath" <<'SH'
#!/bin/sh
# realpath -e -- <path> | realpath -m -- <path>
mode=$1; shift; [ "$1" = "--" ] && shift
if [ "$mode" = "-e" ] && [ ! -e "$1" ]; then exit 1; fi
exec python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$1"
SH
cat > "$T/bin/stat" <<'SH'
#!/bin/sh
# stat -c %s <file>
exec python3 -c 'import os, sys; print(os.path.getsize(sys.argv[1]))' "$3"
SH
cat > "$T/bin/timeout" <<'SH'
#!/bin/sh
# timeout [--kill-after=N] <seconds> <command...>: the stand-in just runs the command
case "$1" in --kill-after=*) shift ;; esac
shift
exec "$@"
SH
cat > "$T/lib/steamapps/common/Proton - Experimental/files/bin/wineserver" <<'SH'
#!/bin/sh
exit 0
SH
cat > "$T/lib/steamapps/common/Proton - Experimental/files/bin/wine64" <<'SH'
#!/bin/sh
[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && { echo "a display variable reached Wine" >&2; exit 9; }
[ "${TZ:-}" = "UTC" ] || { echo "TZ is not UTC" >&2; exit 9; }
case "${1:-}" in
    wineboot) mkdir -p "$WINEPREFIX" && : > "$WINEPREFIX/system.reg"; exit 0 ;;
    --version) echo "wine-0.0-dryrun"; exit 0 ;;
esac
shift
printf '%s\n' "$*" >> "$DRYRUN_LOG"
case "${1:-}" in
    "") echo "usage: refcalls.exe calls <dir> ..." ;;
    calls) printf '# refcalls calls\nL\tOK\tloaded\nR\t1\tPASS\t1\t0000000000000001\tPlat_GetMemPageSize\t\nS\tcalls\tPASS=1\tFAIL=0\tSKIP=0\tCRASH=0\tTIMEOUT=0\tERROR=0\tof=1\n' ;;
    queue) printf '# refcalls queue\nL\tOK\tloaded\nQ\t1\tPASS\t5\t0\t\nS\tqueue\truns=1\tPASS=1\tFAIL=0\tCRASH=0\tTIMEOUT=0\tERROR=0\truns_with_bad_node_link=0\n' ;;
esac
SH
chmod +x "$T/bin/"* "$T/lib/steamapps/common/Proton - Experimental/files/bin/"*

run() { env PATH="$T/bin:$PATH" HOME="$T/home" DISPLAY=:0 DRYRUN_LOG="$T/wine.log" REF_RUNS=3 bash "$T/tool/run_ref.sh" "$@"; }
GAME="$T/lib/steamapps/common/Titanfall2"

out=$(run "$GAME" /mnt/2tb_hdd/scratch 2>&1) && fail "a scratch directory under /mnt/2tb_hdd was accepted"
echo "$out" | grep -q '/mnt/2tb_hdd' || fail "the refusal for /mnt/2tb_hdd does not say why: $out"
[ ! -e /mnt/2tb_hdd ] || fail "/mnt/2tb_hdd exists on this machine"
mkdir -p "$T/used" && echo keep > "$T/used/file"
out=$(run "$GAME" "$T/used" 2>&1) && fail "a scratch directory with someone else's file in it was accepted"
[ "$(cat "$T/used/file")" = keep ] || fail "the refused directory was changed"
out=$(run "$T/nothing-here" "$T/s0" 2>&1) && fail "an install directory that does not exist was accepted"
out=$(run "$GAME" "$GAME/scratch" 2>&1) && fail "a scratch directory inside the install directory was accepted"

out=$(run "$GAME" "$T/scratch" 2>&1) || fail "the run with the stand-in Wine ended with an error: $out"
for f in calls.txt queue.txt env.txt; do [ -s "$T/scratch/results/$f" ] || fail "results/$f is missing or empty"; done
[ ! -e "$T/scratch/prefix" ] && [ ! -e "$T/scratch/work" ] || fail "prefix/ or work/ is still there after the run"
grep -q "^S	calls	" "$T/scratch/results/calls.txt" || fail "calls.txt has no summary line"
grep -q "^S	queue	runs=1" "$T/scratch/results/queue.txt" || fail "queue.txt has no summary line"
grep -q "^dll	tier0.dll	22 bytes	sha256 [0-9a-f]\{64\}$" "$T/scratch/results/env.txt" || fail "env.txt lacks tier0.dll's size and SHA-256: $(cat "$T/scratch/results/env.txt")"
grep -q "^proton_version	dryrun-proton-1$" "$T/scratch/results/env.txt" || fail "env.txt lacks the Proton version"
grep -q "^outcome_queue	complete$" "$T/scratch/results/env.txt" || fail "env.txt does not say the queue mode completed"
want_dir="Z:$(printf '%s' "$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$GAME/bin/x64_retail")" | tr '/' '\\')"
grep -qF "calls $want_dir" "$T/wine.log" || fail "the program was not given the DLL directory as $want_dir: $(cat "$T/wine.log")"
grep -qF "queue $want_dir --runs 3" "$T/wine.log" || fail "the queue mode was not given --runs 3: $(cat "$T/wine.log")"
[ "$(find "$GAME" -type f | wc -l | tr -d ' ')" = 2 ] || fail "the install directory gained or lost a file"

# an interrupted run left a prefix and a work directory: the next run must clear them and succeed
mkdir -p "$T/scratch/prefix/drive_c" "$T/scratch/work" && echo stale > "$T/scratch/results/calls.txt.part"
out=$(run "$GAME" "$T/scratch" 2>&1) || fail "the run over an interrupted run's leftovers ended with an error: $out"
[ ! -e "$T/scratch/prefix" ] && [ ! -e "$T/scratch/work" ] && [ ! -e "$T/scratch/results/calls.txt.part" ] || fail "leftovers of the interrupted run are still there"
[ -s "$T/scratch/results/queue.txt" ] || fail "the second run wrote no queue.txt"
echo "run_ref_dryrun: ok (4 refusals, a run with a stand-in Wine, a run over an interrupted run's leftovers)"
