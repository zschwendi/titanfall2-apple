#!/bin/sh
# Build and run the two native DXMT test programs.  See docs/findings/35-dxmt-build.md.
#
#   sh tools/dxmt/probe.sh
#
# 1. runs tools/dxmt/build.sh (a no-op when DXMT is already built);
# 2. compiles tools/dxmt/device_probe.cpp and tools/dxmt/offscreen_triangle.cpp (system clang++, against DXMT's
#    native headers; the libraries are loaded with dlopen from the build tree);
# 3. device_probe: D3D11CreateDevice, adapter, CheckFeatureSupport / CheckFormatSupport / MSAA, and a compute
#    smoke test (a hand-assembled cs_5_0 writes a value into a UAV buffer; the value is read back);
# 4. offscreen_triangle, twice: first with an empty shader cache, then with the cache the first run filled.
#    It draws a triangle with hand-assembled DXBC shaders into a 256x256 render target, copies it to a staging
#    texture and checks pixels inside and outside the triangle.
#
# Exit status 0 only if a device was created, the compute check passed and both triangle runs passed their
# pixel checks.  Programs and their output go to third_party/dxmt/build-native/tf2-probe/ (git-ignored).
# DXMT's own stderr (warnings about unimplemented Win32 stubs) is kept in *.stderr there and summarised here.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dxmt="$repo/third_party/dxmt"
bld="$dxmt/build-native"
out="$bld/tf2-probe"

sh "$here/build.sh" >/dev/null || { echo "probe.sh: build.sh failed (run it directly to see why)" >&2; exit 1; }

# the committed header must be what make_dxbc.py generates
if [ -x "$repo/.venv/bin/python" ]; then
  "$repo/.venv/bin/python" "$here/make_dxbc.py" --check "$here/dxbc_blobs.h" \
    || { echo "probe.sh: tools/dxmt/dxbc_blobs.h is stale; run: .venv/bin/python tools/dxmt/make_dxbc.py tools/dxmt/dxbc_blobs.h" >&2; exit 1; }
fi

mkdir -p "$out"
for p in device_probe offscreen_triangle; do
  clang++ -std=c++20 -w -I "$dxmt/include/native/windows" -I "$dxmt/include/native/directx" -I "$here" \
    "$here/$p.cpp" -o "$out/$p"
done

# no log file in the current directory, a private shader cache (not the user's Caches directory)
export DXMT_LOG_PATH=none
export DXMT_SHADER_CACHE_PATH="$out/shader-cache"
rm -rf "$out/shader-cache"
mkdir -p "$out/shader-cache"
cd "$out"
status=0

echo "=== device_probe ==="
if "$out/device_probe" "$bld" >device_probe.out 2>device_probe.stderr; then :; else status=1; fi
cat device_probe.out
grep -q '^DEVICE_PROBE_OK compute=pass$' device_probe.out || status=1

echo
echo "=== offscreen_triangle, run 1 (empty shader cache) ==="
if "$out/offscreen_triangle" "$bld" --ppm "$out/triangle" >triangle1.out 2>triangle1.stderr; then :; else status=1; fi
cat triangle1.out
grep -q '^TRIANGLE_OK$' triangle1.out || status=1

echo
echo "=== offscreen_triangle, run 2 (shader cache filled by run 1) ==="
if "$out/offscreen_triangle" "$bld" >triangle2.out 2>triangle2.stderr; then :; else status=1; fi
grep -E '^(D3D11CreateDevice|timing_ms|TRIANGLE_)' triangle2.out
grep -q '^TRIANGLE_OK$' triangle2.out || status=1
ls -l "$out/shader-cache" | sed 's/^/  cache: /'

echo
echo "=== DXMT stderr (counts, all runs) ==="
cat device_probe.stderr triangle1.stderr triangle2.stderr | sort | uniq -c | sort -rn

echo
if [ "$status" -eq 0 ]; then echo "probe.sh: PASS (device created, compute and triangle checks passed)"; else echo "probe.sh: FAIL" >&2; fi
exit "$status"
