#!/bin/sh
# Time the first draw (DXBC -> AIR conversion + Metal pipeline compile + the draw itself, up to the pixels being
# readable) under different cache states.  Needs `sh tools/dxmt/probe.sh` to have run once (it builds the programs).
#
#   sh tools/dxmt/timing.sh [rounds]        (default 5)
#
# Each round picks a new salt, so the pixel shader is one that Metal's own pipeline cache has not seen, then runs
# offscreen_triangle four times:
#   cold      new shader, empty DXMT cache                       (nothing has seen this shader)
#   metal     same shader, empty DXMT cache                      (Metal's cache knows it; DXMT converts again)
#   warm      same shader, DXMT cache filled by the 'cold' run   (DXMT skips the conversion)
#   nocache   same shader, DXMT_SHADER_CACHE=0
# and prints the median of first_draw_to_readback over the rounds, in milliseconds.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
bld="$repo/third_party/dxmt/build-native"
exe="$bld/tf2-probe/offscreen_triangle"
[ -x "$exe" ] || { echo "timing.sh: run tools/dxmt/probe.sh first" >&2; exit 1; }
rounds=${1:-5}
export DXMT_LOG_PATH=none
export DXMT_LOG_LEVEL=error
tmp=$(mktemp -d "${TMPDIR:-/tmp}/dxmt-timing.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

first_draw() {  # args: salt, cache dir  (cache dir "off" disables the DXMT cache)
  if [ "$2" = off ]; then
    DXMT_SHADER_CACHE=0 "$exe" "$bld" --salt "$1" 2>/dev/null
  else
    DXMT_SHADER_CACHE_PATH="$2" "$exe" "$bld" --salt "$1" 2>/dev/null
  fi | sed -n 's/.*first_draw_to_readback=\([0-9.]*\).*/\1/p'
}

median() { sort -n | awk '{a[NR]=$1} END {print (NR%2) ? a[(NR+1)/2] : (a[NR/2]+a[NR/2+1])/2}'; }

base=$(date +%s)
: >"$tmp/cold"; : >"$tmp/metal"; : >"$tmp/warm"; : >"$tmp/nocache"
i=1
while [ "$i" -le "$rounds" ]; do
  salt=$((base * 10 + i * 7919))
  c1="$tmp/c1.$i"; c2="$tmp/c2.$i"; mkdir -p "$c1" "$c2"
  first_draw "$salt" "$c1" >>"$tmp/cold"
  first_draw "$salt" "$c2" >>"$tmp/metal"
  first_draw "$salt" "$c1" >>"$tmp/warm"
  first_draw "$salt" off  >>"$tmp/nocache"
  i=$((i + 1))
done

echo "first_draw_to_readback, median of $rounds rounds (ms); all rows: $(wc -l <"$tmp/cold" | tr -d ' ') values each"
for k in cold metal warm nocache; do
  printf '  %-8s %8s   (all: %s)\n' "$k" "$(median <"$tmp/$k")" "$(tr '\n' ' ' <"$tmp/$k")"
done
