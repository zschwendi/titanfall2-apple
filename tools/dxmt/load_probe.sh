#!/bin/sh
# Build load_probe, put DXMT's three libraries side by side in one directory, and run the probe there, outside any
# app bundle.  For step 0c of the picture track; see tools/dxmt/load_probe.cpp and docs/handoff/pic-fork.md.
#
#   sh tools/dxmt/load_probe.sh                          the release build's libraries
#   DXMT_BUILD_KIND=assert sh tools/dxmt/load_probe.sh   the assert build's (what the picture track runs)
#
# 1. runs tools/dxmt/build.sh for the kind;
# 2. copies winemetal.dylib, dxgi.dylib and d3d11.dylib into build/dxmt/Frameworks/ and compiles
#    build/dxmt/load_probe (build/ is git-ignored);
# 3. checks what the three copies link against: only /usr/lib, /System/Library and each other by @rpath; their
#    install names are @rpath/<name>; dxgi and d3d11 carry the rpath @loader_path; no LLVM, zstd or libunwind
#    library is a dependency (they are linked in statically);
# 4. runs `load_probe DIR` with every DYLD_* variable unset: must print "load_probe: ok" and exit 0;
# 5. runs `load_probe DIR --tso`: outside the signed bundle the kernel refuses the x86 memory-ordering mode, and the
#    probe must say so and exit 3.  That refusal is this script's expected result.  (Exit 0 here would mean the
#    script was run from inside the signed bundle; it is reported, not failed.)
#
# Nothing is signed here and the signed bundle is not run: both are the planner's (app/app.mk).
# Exit status 0 only if 3, 4 and 5 held.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dxmt="$repo/third_party/dxmt"
kind="${DXMT_BUILD_KIND:-release}"
bld=$(sh "$here/build.sh" --print-dir)
out="$repo/build/dxmt"
fw="$out/Frameworks"

sh "$here/build.sh" >/dev/null || { echo "load_probe.sh: build.sh failed (run it directly to see why)" >&2; exit 1; }
mkdir -p "$fw"
cp "$bld/src/nativemetal/winemetal.dylib" "$bld/src/dxgi/dxgi.dylib" "$bld/src/d3d11/d3d11.dylib" "$fw/"
clang++ -std=c++20 -w -I "$dxmt/include/native/windows" -I "$dxmt/include/native/directx" "$here/load_probe.cpp" -o "$out/load_probe"
status=0

echo "=== the three libraries ($kind build), copied to $fw ==="
for lib in winemetal dxgi d3d11; do
  f="$fw/$lib.dylib"
  id=$(otool -D "$f" | tail -1)
  deps=$(otool -L "$f" | tail -n +2 | awk '{print $1}' | grep -v "^$id\$" || true)
  rpaths=$(otool -l "$f" | awk '/LC_RPATH/ {r=1} r && $1 == "path" {print $2; r=0}' | tr '\n' ' ')
  echo "$lib.dylib: $(stat -f %z "$f") bytes, install name $id, rpaths: $rpaths"
  echo "$deps" | sed 's/^/    needs /'
  [ "$id" = "@rpath/$lib.dylib" ] || { echo "  FAIL install name is not @rpath/$lib.dylib"; status=1; }
  bad=$(echo "$deps" | grep -v -e '^/usr/lib/' -e '^/System/Library/' -e '^@rpath/winemetal\.dylib$' -e '^@rpath/dxgi\.dylib$' || true)
  [ -z "$bad" ] || { echo "  FAIL dependency outside the system and the three libraries: $bad"; status=1; }
  case " $rpaths" in
    *" /"*) echo "  FAIL an absolute rpath"; status=1 ;;
  esac
  if [ "$lib" != winemetal ]; then
    case " $rpaths " in
      *" @loader_path "*) ;;
      *) echo "  FAIL no @loader_path rpath (the fork is not at patch 5)"; status=1 ;;
    esac
  fi
done
llvm=$(nm -U "$fw/winemetal.dylib" | grep -c ' [TtSs] __ZN4llvm' || true)
echo "winemetal.dylib defines $llvm llvm:: symbols itself (LLVM is linked in statically)"
[ "$llvm" -gt 1000 ] || { echo "  FAIL expected LLVM inside winemetal.dylib"; status=1; }

echo
echo "=== load_probe DIR (no DYLD_* variable) ==="
if env -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH "$out/load_probe" "$fw" >"$out/load_probe.out" 2>"$out/load_probe.stderr"; then rc=0; else rc=$?; fi
cat "$out/load_probe.out"
echo "exit status $rc"
{ [ "$rc" -eq 0 ] && grep -q '^load_probe: ok ' "$out/load_probe.out"; } || { echo "  FAIL"; sed 's/^/  stderr: /' "$out/load_probe.stderr" | tail -5; status=1; }

echo
echo "=== load_probe DIR --tso (outside the signed bundle: the refusal is expected) ==="
if "$out/load_probe" "$fw" --tso >"$out/load_probe_tso.out" 2>"$out/load_probe_tso.stderr"; then rc=0; else rc=$?; fi
cat "$out/load_probe_tso.out"
echo "exit status $rc"
if [ "$rc" -eq 3 ] && grep -q 'x86 memory ordering REFUSED' "$out/load_probe_tso.out"; then
  echo "  refused, as expected outside the bundle"
elif [ "$rc" -eq 0 ]; then
  echo "  NOTE the mode was GRANTED: this run was inside a signed bundle"
else
  echo "  FAIL neither the expected refusal (3) nor success"; status=1
fi

echo
if [ "$status" -eq 0 ]; then echo "load_probe.sh: PASS ($kind build); program $out/load_probe, libraries $fw"; else echo "load_probe.sh: FAIL" >&2; fi
exit "$status"
