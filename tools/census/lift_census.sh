#!/bin/zsh
# Lift every game DLL with the current lifter and print one line per module.
#
#     tools/census/lift_census.sh [OUT]      (OUT defaults to build/lift-census)
#
# Each module's log and report stay under OUT; the generated C is deleted as it goes (the three
# large DLLs are hundreds of megabytes of C each).  OriginSDK.dll and d3dcompiler_46.dll are
# left out: neither is lifted in this project.  Takes about five minutes.
cd "$(dirname "$0")/../.." || exit 1
out=${1:-build/lift-census}
mkdir -p "$out"
: > "$out/progress.txt"
for dll in game/bin/x64_retail/*.dll game/server.dll; do
  m=$(basename "$dll" .dll)
  case $m in OriginSDK|d3dcompiler_46) continue;; esac
  .venv/bin/python -m lifter.lift --module "$m" --dll "$dll" --out "$out/$m" > "$out/$m.log" 2>&1
  echo "$m exit $?" >> "$out/progress.txt"
  find "$out/$m" \( -name '*.c' -o -name '*.h' \) -delete 2>/dev/null
  printf '%-22s %s\n' "$m" "$(grep -E '^ *lifted [0-9]+ \(' "$out/$m.log" | sed 's/^ *//')"
done
