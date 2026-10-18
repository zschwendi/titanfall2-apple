#!/bin/sh
# Build and run the swap chain window test.  See docs/findings/35-dxmt-window.md and docs/handoff/pic-fork.md.
#
#   sh tools/dxmt/window.sh [--show] [window_test options]          against the release build
#   DXMT_BUILD_KIND=assert sh tools/dxmt/window.sh [...]            against the assert build
#
# 1. runs tools/dxmt/build.sh for the kind (applies patches/dxmt/*.patch to the DXMT clone when needed, builds
#    incrementally);
# 2. compiles tools/dxmt/window_test.cpp and window_test_layer.mm (system clang++, DXMT's native headers, SDL3 through
#    pkg-config) and tools/dxmt/window_probe.cpp;
# 3. window_probe: swap chain requests for things that are not windows, each in a forked child; none may be killed by
#    a signal and none may succeed (no window is opened);
# 4. window_test, twice: with the game's swap chain (R8G8B8A8_UNORM_SRGB, 3 buffers; the bytes expected are the sRGB
#    encoding of the colours drawn) and with the pair the test was written with (R8G8B8A8_UNORM, 2 buffers).  It
#    presents frames into a 640x360 window through DXMT and checks everything itself; see the top of
#    window_test.cpp.  With --format or --buffers on the command line it runs once, with what was asked;
# 5. runs tools/dxmt/hooks.sh for the same kind (the host hears DXMT: patch 4 of the fork).
#
# THE WINDOW IS NOT SHOWN unless --show (or --foreground) is given: the default is window_test's --hidden, in which
# everything runs and is checked except the times at which frames reach the screen (Core Animation shows no frame of
# a hidden window).  The last line says so.  With --show the window is on screen for about five seconds per run,
# not activated, no input focus.  For a human to look at the picture:  sh tools/dxmt/window.sh --foreground --hold 5
#
# Exit status 0 only if every program passed.  Programs and output go to <build dir>/tf2-window/ (git-ignored),
# DXMT's shader cache too; nothing is written to the user's cache directory by DXMT.  The window test ends itself
# after 30 s whatever happens, and this script kills it after 75 s, so no window can be left open.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dxmt="$repo/third_party/dxmt"
kind="${DXMT_BUILD_KIND:-release}"
bld=$(sh "$here/build.sh" --print-dir)
out="$bld/tf2-window"

# our own option --show is taken out; --hidden is added unless the caller wants the window on screen
shown=0
custom=0
n=$#
while [ "$n" -gt 0 ]; do
  a=$1; shift; n=$((n - 1))
  case "$a" in
    --show) shown=1 ;;
    --foreground) shown=1; set -- "$@" "$a" ;;
    --format|--buffers) custom=1; set -- "$@" "$a" ;;
    *) set -- "$@" "$a" ;;
  esac
done
[ "$shown" -eq 1 ] || set -- "$@" --hidden

sh "$here/build.sh" >/dev/null || { echo "window.sh: build.sh failed (run it directly to see why)" >&2; exit 1; }
[ -f "$dxmt/include/native/dxmt_native_window.h" ] \
  || { echo "window.sh: DXMT is not patched (include/native/dxmt_native_window.h missing)" >&2; exit 1; }
command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl3 \
  || { echo "window.sh: SDL3 not found through pkg-config (this script installs nothing)" >&2; exit 1; }

if [ -x "$repo/.venv/bin/python" ]; then
  "$repo/.venv/bin/python" "$here/make_dxbc.py" --check "$here/dxbc_blobs.h" \
    || { echo "window.sh: tools/dxmt/dxbc_blobs.h is stale" >&2; exit 1; }
fi

mkdir -p "$out"
inc="-I $dxmt/include/native/windows -I $dxmt/include/native/directx -I $dxmt/include/native -I $here"
# shellcheck disable=SC2086
clang++ -std=c++20 -w $inc "$here/window_probe.cpp" -o "$out/window_probe"
clang++ -std=c++20 -fobjc-arc -Wall -c "$here/window_test_layer.mm" -o "$out/window_test_layer.o"
# shellcheck disable=SC2086,SC2046
clang++ -std=c++20 -w $inc $(pkg-config --cflags sdl3) -c "$here/window_test.cpp" -o "$out/window_test.o"
# shellcheck disable=SC2046
clang++ "$out/window_test.o" "$out/window_test_layer.o" $(pkg-config --libs sdl3) \
  -framework AppKit -framework QuartzCore -framework Metal -framework CoreGraphics -framework Foundation -o "$out/window_test"

export DXMT_LOG_PATH=none
export DXMT_SHADER_CACHE_PATH="$out/shader-cache"
# the Metal performance HUD draws into the layer's drawables, which the test compares pixel by pixel
unset MTL_HUD_ENABLED
mkdir -p "$out/shader-cache"
cd "$out"
status=0
summary=""
unchecked=0

echo "=== window_probe (no window, $kind build) ==="
if "$out/window_probe" "$bld" >window_probe.out 2>window_probe.stderr; then :; else status=1; fi
cat window_probe.out
grep -q '^WINDOW_PROBE_OK$' window_probe.out || status=1

# run_test <name> <title> [window_test options]: output in window_test.<name>.out / .stderr
run_test() {
  name=$1; title=$2; shift 2
  echo
  echo "=== window_test: $title ($kind build; the window is $([ "$shown" -eq 1 ] && echo shown for a few seconds || echo not shown)) ==="
  "$out/window_test" "$bld" "$@" >"window_test.$name.out" 2>"window_test.$name.stderr" &
  pid=$!
  ( sleep 75; kill -9 "$pid" 2>/dev/null && echo "window.sh: killed window_test after 75 s" >&2 ) &
  guard=$!
  if wait "$pid"; then :; else status=1; fi
  kill "$guard" 2>/dev/null || true
  wait "$guard" 2>/dev/null || true
  cat "window_test.$name.out"
  grep -q '^WINDOW_TEST_OK$' "window_test.$name.out" || status=1
  grep -q 'ON-SCREEN PACING NOT CHECKED' "window_test.$name.out" && unchecked=1
  summary="$summary $title: $(grep -E '^[0-9]+ checks, [0-9]+ failed$' "window_test.$name.out" || echo 'no count');"
  echo
  echo "--- DXMT stderr of this run (counts) ---"
  sort "window_test.$name.stderr" | uniq -c | sort -rn | head -40
}

if [ "$custom" -eq 1 ]; then
  run_test custom "as asked" "$@"
else
  run_test game "the game's swap chain, R8G8B8A8_UNORM_SRGB with 3 buffers" --format R8G8B8A8_UNORM_SRGB --buffers 3 "$@"
  run_test old "R8G8B8A8_UNORM with 2 buffers" "$@"
fi

echo
if sh "$here/hooks.sh" >hooks.out 2>hooks.stderr; then :; else status=1; fi
grep -E '^(=== |  FAIL|[0-9]+ checks|HOOK_TEST|hooks.sh)' hooks.out hooks.stderr | sed 's/^hooks\.[a-z]*://'
summary="$summary hooks: $(grep -E '^[0-9]+ checks, [0-9]+ failed' hooks.out || echo 'no count')"

echo
echo "window.sh ($kind build):$summary"
if [ "$status" -ne 0 ]; then
  echo "window.sh: FAIL" >&2
elif [ "$unchecked" -eq 1 ]; then
  # everything the program can check without frames being shown has passed; say what was not observed
  echo "window.sh: PASS, but on-screen pacing was NOT checked (window not shown, display asleep, screen locked, or window covered); see above"
else
  echo "window.sh: PASS"
fi
exit "$status"
