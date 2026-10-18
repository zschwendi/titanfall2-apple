#!/bin/sh
# The synthetic click's anchor, N runs (unit gate3-run-13, item 2).  Each run starts from `new`,
# plays gate3/synthetic/new-game.script (one click on "New Game", anchored on the UI's script
# opening the main menu: gate3/run.c, THE MENU MARK) and is ended by the time limit after
# CLICK_SECONDS (60; the menu opens 24 s into a run on a quiet machine).  A run counts as
# a hit when, after the mark fired, the UI closed menu_MainMenu and opened menu_PlayVideoMenu
# (what "New Game" does on a first start: docs/handoff/gate3-run-10.md, section 2); anything
# else is a miss.  Runs the game: needs the game's files.  One line per run, then the total.
#
#     gate3/check_click.sh <gate3 binary> <scratch directory> [N=20] [jobs=2]
#
# Exit status 0 when every run hit.
set -u
bin=${1:?usage: check_click.sh <gate3 binary> <scratch directory> [N] [jobs]}
dir=${2:?usage: check_click.sh <gate3 binary> <scratch directory> [N] [jobs]}
n=${3:-20}
jobs=${4:-2}
mkdir -p "$dir" || exit 2
one() {
    k=$1
    env RT_LOG=all:info GATE3_RUN_OUT="$dir/click$k" GATE3_TIMEOUT="${CLICK_SECONDS:-60}" GATE3_REPORT_EVERY=0 \
        GATE3_INPUT_SCRIPT=gate3/synthetic/new-game.script nice "$bin" run > "$dir/click$k.out" 2>&1
    log="$dir/click$k.log"
    mark=$(grep -m1 'gate3 run: menu: opened menu_MainMenu ' "$log" | sed 's/.*menu_MainMenu at \([0-9.]*\) s into the run, after Present \([0-9]*\).*/\1 s, Present \2/')
    closed=$(grep -m1 'gate3 run: menu: closed menu_MainMenu ' "$log" | sed 's/.*menu_MainMenu at \([0-9.]*\) s.*/\1 s/')
    video=$(grep -c 'gate3 run: menu: opened menu_PlayVideoMenu ' "$log")
    delivered=$(grep -m1 '^INPUT' "$dir/click$k.out" | sed 's/.*delivered \([0-9]*\) (posted.*/\1/')
    if [ -n "$mark" ] && [ -n "$closed" ] && [ "$video" = 1 ] && [ "$delivered" = 3 ]; then verdict=hit; else verdict=MISS; fi
    echo "click-check: run $k: $verdict: the main menu opened at ${mark:-never}; closed at ${closed:-never}; the video menu opened $video time(s); $delivered of 3 events delivered" > "$dir/click$k.verdict"
}
k=1
while [ "$k" -le "$n" ]; do
    j=0
    while [ "$j" -lt "$jobs" ] && [ "$k" -le "$n" ]; do one "$k" & k=$((k + 1)); j=$((j + 1)); done
    wait
done
hits=0
k=1
while [ "$k" -le "$n" ]; do
    cat "$dir/click$k.verdict"
    grep -q ': hit: ' "$dir/click$k.verdict" && hits=$((hits + 1))
    k=$((k + 1))
done
echo "click-check: $hits of $n runs hit"
[ "$hits" = "$n" ]
