#!/bin/bash
# convert.sh: the whole recording to replay script files under build/trace/script/, in ranges.
#
#   tools/trace/d3d/convert.sh [fetch|script|all]      (default: all)
#
# fetch   reads the recording through remote.sh, one range of STEP calls at a time, one command
#         at a time, as `clean --full` (shader text never crosses: see remote.sh), and keeps each
#         range gzipped under build/trace/clean/.  A range that is already there is not read again.
# script  converts the kept ranges in order with d3dscript.py (object numbers and open mappings
#         carry over in build/trace/script/state.json) into build/trace/script/<trace>-<first call>.d3ds.gz
#
# Everything written is under build/trace/ (git-ignored: derived from the game, never committed).
# Replay: make d3d-replay
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PY=${PY:-.venv/bin/python}
trace=${D3DTRACE:-mission1}
step=${STEP:-200000}
last=${LAST:-2800000}           # past the recording's end (2,767,254 lines); an empty range ends the loop
what=${1:-all}
clean=build/trace/clean
out=build/trace/script
mkdir -p "$clean" "$out"

if [ "$what" = fetch ] || [ "$what" = all ]; then
    a=0
    while [ "$a" -lt "$last" ]; do
        b=$((a + step - 1))
        f=$(printf '%s/%s-%08d.txt.gz' "$clean" "$trace" "$a")
        if [ ! -s "$f" ]; then
            t0=$(date +%s)
            "$here/remote.sh" -t "$trace" -c "$a-$b" clean --full | gzip -6 > "$f.tmp"
            n=$(gzip -dc "$f.tmp" | wc -l | tr -d ' ')
            if [ "$n" = 0 ]; then rm -f "$f.tmp"; echo "convert.sh: calls $a-$b: none, the end"; break; fi
            mv "$f.tmp" "$f"
            echo "convert.sh: calls $a-$b: $n lines, $(wc -c < "$f" | tr -d ' ') bytes gzipped, $(( $(date +%s) - t0 )) s"
        fi
        a=$((a + step))
    done
fi

if [ "$what" = script ] || [ "$what" = all ]; then
    rm -f "$out"/state.json "$out"/"$trace"-*.d3ds.gz
    t0=$(date +%s)
    for f in "$clean"/"$trace"-*.txt.gz; do
        a=$(basename "$f" .txt.gz); a=${a#"$trace"-}
        gzip -dc "$f" | "$PY" "$here/d3dscript.py" convert --state "$out/state.json" -o "$out/$trace-$a.d3ds.gz"
    done
    echo "convert.sh: script files: $(cat "$out"/"$trace"-*.d3ds.gz | wc -c | tr -d ' ') bytes gzipped, $(( $(date +%s) - t0 )) s"
fi
