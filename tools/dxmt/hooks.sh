#!/bin/sh
# Build and run the proof of patch 4 of the fork: the host program hears DXMT's log lines and the reason for an
# abort.  See tools/dxmt/hook_test.cpp (what each case is) and docs/handoff/pic-fork.md.
#
#   sh tools/dxmt/hooks.sh                          against the release build
#   DXMT_BUILD_KIND=assert sh tools/dxmt/hooks.sh   against the assert build
#
# 1. runs tools/dxmt/build.sh for the kind;
# 2. checks with nm where the three libraries get the hook functions and the assert handler from: all from
#    winemetal.dylib, so that one registration covers dxgi, d3d11 and winemetal;
# 3. compiles and runs hook_test.  No window.  Every case runs in a child process; in one or two of them DXMT calls
#    abort(), which the child catches (so macOS writes no crash report).
#
# Exit status 0 only if the symbol check and every check of hook_test held.  Output goes to <build dir>/tf2-hooks/.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dxmt="$repo/third_party/dxmt"
kind="${DXMT_BUILD_KIND:-release}"
bld=$(sh "$here/build.sh" --print-dir)
out="$bld/tf2-hooks"

sh "$here/build.sh" >/dev/null || { echo "hooks.sh: build.sh failed (run it directly to see why)" >&2; exit 1; }
[ -f "$dxmt/include/native/dxmt_native_host.h" ] \
  || { echo "hooks.sh: DXMT is not at patch 4 (include/native/dxmt_native_host.h missing)" >&2; exit 1; }
if [ -x "$repo/.venv/bin/python" ]; then
  "$repo/.venv/bin/python" "$here/make_dxbc.py" --check "$here/dxbc_blobs.h" \
    || { echo "hooks.sh: tools/dxmt/dxbc_blobs.h is stale" >&2; exit 1; }
fi

mkdir -p "$out"
status=0

echo "=== where the hook functions live ($kind build) ==="
wm="$bld/src/nativemetal/winemetal.dylib"
for sym in _DXMTSetHostLogFunction _DXMTSetHostAbortFunction _DXMTHostLog _DXMTHostAbort ___assert_rtn; do
  if nm -gU "$wm" | grep -q " T $sym\$"; then echo "  winemetal.dylib defines $sym"; else echo "  FAIL winemetal.dylib does not define $sym"; status=1; fi
done
# nm -m prints "(undefined) external _name (from library)" for an import
want() {  # want <library file> <symbol> <library it must come from>
  line=$(nm -m "$1" | grep "(undefined) external $2 " || true)
  case "$line" in
    *"(from $3)"*) echo "  $(basename "$1") takes $2 from $3" ;;
    "") echo "  $(basename "$1") does not use $2" ;;
    *) echo "  FAIL $(basename "$1"): $line"; status=1 ;;
  esac
}
want "$bld/src/dxgi/dxgi.dylib" _DXMTHostLog winemetal
want "$bld/src/dxgi/dxgi.dylib" ___assert_rtn winemetal
want "$bld/src/d3d11/d3d11.dylib" _DXMTHostAbort winemetal
want "$bld/src/d3d11/d3d11.dylib" ___assert_rtn winemetal
# the logger itself: one copy, in dxgi.dylib; d3d11 must import it, not carry its own
logerr=__ZN4dxmt6Logger7emitMsgENS_8LogLevelERKNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE
if nm -gU "$bld/src/dxgi/dxgi.dylib" | grep -q "$logerr" && ! nm -U "$bld/src/d3d11/d3d11.dylib" | grep -q "$logerr"; then
  echo "  the logger's code is in dxgi.dylib only (d3d11.dylib calls it there)"
else
  echo "  NOTE d3d11.dylib carries its own copy of the logger; it still calls the same DXMTHostLog in winemetal.dylib"
fi

echo
echo "=== hook_test ($kind build) ==="
# shellcheck disable=SC2086
clang++ -std=c++20 -w -I "$dxmt/include/native/windows" -I "$dxmt/include/native/directx" -I "$dxmt/include/native" -I "$here" \
  "$here/hook_test.cpp" -o "$out/hook_test"
cd "$out"
if "$out/hook_test" "$bld" --kind "$kind" >hook_test.out 2>hook_test.stderr; then :; else status=1; fi
cat hook_test.out
grep -q '^HOOK_TEST_OK$' hook_test.out || status=1

echo
if [ "$status" -eq 0 ]; then echo "hooks.sh: PASS ($kind build)"; else echo "hooks.sh: FAIL ($kind build)" >&2; fi
exit "$status"
