#!/bin/sh
# Where a native DXMT puts its shader cache and its log, and proof that the cache is read back.
# See docs/handoff/pic-fork.md.
#
#   sh tools/dxmt/paths.sh DIR                          (release build)
#   DXMT_BUILD_KIND=assert sh tools/dxmt/paths.sh DIR   (assert build)
#
# DIR is created if missing; DIR/cache, DIR/run1 and DIR/run2 are emptied first.  Two runs of
# tools/dxmt/offscreen_triangle (a triangle drawn off screen with the made-up shaders; no window) with
#
#     DXMT_SHADER_CACHE_PATH=DIR/cache      an absolute directory; DXMT writes shaders_<metal version>.db there
#     DXMT_LOG_PATH=DIR/run<N>              a directory; DXMT writes <program name>_dxgi.log there, and in a native
#                                           build the program name is empty, so the file is _dxgi.log
#     DXMT_LOG_LEVEL=debug                  so that the log holds the two lines patch 4 added to the shader compile
#                                           task: "Shader variant converted: ..." (DXBC was converted to Metal's
#                                           intermediate form and the result stored in the cache) and
#                                           "Shader variant from cache: ..." (read from the cache, not converted)
#
# Expected, and checked: run 1 converts both shaders (vertex, pixel) and takes none from the cache; the cache file
# then exists under DIR/cache with two rows; run 2 converts none and takes both from the cache; both runs pass
# their pixel checks.  Exit status 0 only then.  The times are printed, not checked: for shaders of ten
# instructions the conversion is 1 to 2 ms and Metal's own pipeline compile dominates (finding 35-dxmt-build, 8).
set -eu

[ $# -eq 1 ] || { echo "usage: sh tools/dxmt/paths.sh DIR" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dxmt="$repo/third_party/dxmt"
kind="${DXMT_BUILD_KIND:-release}"
bld=$(sh "$here/build.sh" --print-dir)
mkdir -p "$1"
dir=$(cd "$1" && pwd)

sh "$here/build.sh" >/dev/null || { echo "paths.sh: build.sh failed (run it directly to see why)" >&2; exit 1; }
mkdir -p "$bld/tf2-probe"
prog="$bld/tf2-probe/offscreen_triangle"
clang++ -std=c++20 -w -I "$dxmt/include/native/windows" -I "$dxmt/include/native/directx" -I "$here" "$here/offscreen_triangle.cpp" -o "$prog"

rm -rf "$dir/cache" "$dir/run1" "$dir/run2"
mkdir -p "$dir/cache" "$dir/run1" "$dir/run2"
status=0
count() { grep -c "$1" "$2" 2>/dev/null || true; }

for n in 1 2; do
  ( cd "$dir/run$n" && DXMT_SHADER_CACHE_PATH="$dir/cache" DXMT_LOG_PATH="$dir/run$n" DXMT_LOG_LEVEL=debug \
      "$prog" "$bld" >triangle.out 2>triangle.stderr ) || status=1
  log="$dir/run$n/_dxgi.log"
  [ -f "$log" ] || { echo "paths.sh: FAIL no log at $log"; status=1; }
  conv=$(count 'Shader variant converted' "$log"); hit=$(count 'Shader variant from cache' "$log")
  eval "conv$n=$conv; hit$n=$hit"
  echo "run $n ($kind build): converted $conv, from cache $hit; $(grep -c . "$log") log lines in $log; $(grep '^timing_ms' "$dir/run$n/triangle.out" || echo 'no timing line')"
  grep -q '^TRIANGLE_OK$' "$dir/run$n/triangle.out" || { echo "paths.sh: FAIL run $n did not pass its pixel checks"; status=1; }
  if [ "$n" -eq 1 ]; then
    db=$(ls "$dir/cache"/shaders_*.db 2>/dev/null | head -1)
    [ -n "$db" ] || { echo "paths.sh: FAIL no shaders_*.db under $dir/cache after run 1"; status=1; }
    if [ -n "$db" ] && command -v sqlite3 >/dev/null 2>&1; then
      table=$(sqlite3 "$db" "select name from sqlite_master where type='table' limit 1")
      rows1=$(sqlite3 "$db" "select count(*) from $table")
      echo "  cache after run 1: $db, table $table, $rows1 rows, $(stat -f %z "$db") bytes"
    fi
  fi
done
if [ -n "${db:-}" ] && command -v sqlite3 >/dev/null 2>&1; then
  echo "  cache after run 2: $(sqlite3 "$db" "select count(*) from $table") rows"
fi
ls "$dir/cache" | sed 's/^/  in cache directory: /'

# shellcheck disable=SC2154
{ [ "$conv1" -eq 2 ] && [ "$hit1" -eq 0 ]; } || { echo "paths.sh: FAIL run 1 should convert 2 and take 0 from the cache"; status=1; }
# shellcheck disable=SC2154
{ [ "$conv2" -eq 0 ] && [ "$hit2" -eq 2 ]; } || { echo "paths.sh: FAIL run 2 should convert 0 and take 2 from the cache"; status=1; }
if [ "$status" -eq 0 ]; then echo "paths.sh: PASS ($kind build): the cache under $dir/cache was written by run 1 and read by run 2"; else echo "paths.sh: FAIL" >&2; fi
exit "$status"
