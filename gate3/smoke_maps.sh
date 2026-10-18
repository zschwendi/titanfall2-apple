#!/bin/sh
# smoke_maps.sh [MAP ...]: start each campaign map in a hidden run of the SIGNED BUNDLE and report how far it got.
#
# For each map: the main menu, then `map <name>` through the engine's own command buffer (GATE3_CONSOLE,
# gate3/run.c), nobody at the controls, T seconds in all (default 170).  The real renderer (DXMT), x86 memory
# ordering, sound started but sent nowhere (RT_HOST_AUDIO=none), WORKERS job threads (default 3).  It starts
# from a COPY of the player's state (GATE3_START=save:), so nothing of his is written.  Nothing is shown and
# nothing is played.  It finds what stops a map's load and its first scene; it does not play.
#
# ONLY THE PLANNER OR THE OWNER RUNS THIS (it runs the signed bundle), and never while the owner plays.
# Output: build/gate3/smoke/<map>.{log,out} and one line a map on standard output.
cd "$(dirname "$0")/.." || exit 2
root=$(pwd)
bundle=${SMOKE_BUNDLE:-$root/build/app/TFRecomp.app}     # SMOKE_BUNDLE: another signed bundle (an absolute path)
maps=${*:-sp_sewers1 sp_boomtown_start sp_boomtown sp_boomtown_end sp_hub_timeshift sp_timeshift_spoke02 sp_beacon sp_beacon_spoke0 sp_tday sp_s2s sp_skyway_v1 sp_crashsite sp_training}
mkdir -p build/gate3/smoke
for m in $maps; do
    rm -rf "build/gate3/smoke/$m.state" "build/gate3/smoke/$m.log"
    env RT_TSO="${SMOKE_TSO:-require}" RT_D3D_BACKEND=dxmt RT_DXMT_DIR="$bundle/Contents/Frameworks" RT_HOST_WINDOW=appkit-hidden \
        GATE3_SOUND=on RT_HOST_AUDIO=none GATE3_WORKERS="${WORKERS:-3}" GATE3_REPORT_EVERY=10 GATE3_TIMEOUT="${T:-170}" \
        GATE3_START="${SMOKE_START:-save:$root/build/gate3/player}" GATE3_CONSOLE="3:map $m" GATE3_RUN_OUT="build/gate3/smoke/$m" \
        "$bundle/Contents/MacOS/gate3" run > "build/gate3/smoke/$m.out" 2>&1
    st=$?
    log="build/gate3/smoke/$m.log"
    loaded=$(grep -m1 "menu: closed menu_MainMenu" "$log" | sed -E 's/.* at ([0-9.]+) s into.*/\1/')
    stop=$(grep -m1 "FATAL\|THE STOP" "$log" | cut -c1-200)
    last=$(grep "report: at" "$log" | tail -1 | sed -E 's/.*report: at ([0-9.]+) s, Present ([0-9]+): [0-9]+ Presents in the last [0-9.]+ s \(([0-9.]+) a second\), ([0-9]+) of them frames of the mission \(([0-9]+) so far\); per Present: ([0-9.]+) device calls, ([0-9.]+) draws.*footprint ([0-9.]+) GB.*/last report \1 s: \3 a second, \5 mission frames, \6 calls and \7 draws a frame, \8 GB/')
    echo "$m: exit $st; map up at ${loaded:-never} s; ${stop:-no stop}; $last"
    rm -rf "build/gate3/smoke/$m.state"
done
