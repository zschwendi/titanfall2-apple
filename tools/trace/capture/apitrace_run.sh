#!/bin/bash
# One run of Titanfall 2 under Proton with apitrace's Direct3D 11 wrappers beside the game's
# DLLs: every Direct3D and DXGI call the game makes is written to a .trace file.
# usage: apitrace_run.sh <label> auto:<secs>|watch
#   auto:<secs>  start the game through the running Steam client, stop it <secs> s after its
#                process appears (an unattended test; Steam may first process shaders for
#                many minutes unless someone clicks Skip, so it waits up to 30 minutes)
#   watch        wait for the owner to start the game himself, record until it exits
# What it changes outside ~/tf2-trace, and puts back when the run ends:
#   user_settings.py in Proton Experimental (logging on, TRACE_FILE set), and three files
#   copied into the game's bin/x64_retail (d3d11.dll, dxgi.dll, dxgitrace.dll from
#   ~/tf2-trace/apitrace/wrappers: apitrace 14.0, win64).  It refuses to start if a file of
#   one of those names is already there, and removes only files identical to its own.
# The trace goes to ~/tf2-trace/<label>/Titanfall2.trace; if the game's process did not get
# TRACE_FILE, apitrace writes <game root>/Titanfall2*.trace and the script moves it to
# <library>/tf2-trace-apitrace/<label>/ (same disk) and links it from ~/tf2-trace/<label>/.
set -u
LABEL=${1:?label}; MODE=${2:?auto:<secs> or watch}
T=$HOME/tf2-trace
PROTON="$HOME/.local/share/Steam/steamapps/common/Proton - Experimental"
LIB=/mnt/1tb_nvme/SteamLibrary
COMPAT=$LIB/steamapps/compatdata/1237970
GAME=$LIB/steamapps/common/Titanfall2
BIN=$GAME/bin/x64_retail
W=$T/apitrace/wrappers
US="$PROTON/user_settings.py"
ST=$T/status-$LABEL.txt
LOGDIR=$T/$LABEL
CHANNELS=+timestamp,+pid,+tid,+loaddll,+seh
MAX_BYTES=$((40 * 1024 * 1024 * 1024)); MAX_RUN_SECS=1500; MAX_WAIT_SECS=1800
say() { echo "$(date +%H:%M:%S) $*" >> "$ST"; }
game_pids() {
    for d in /proc/[0-9]*; do
        { tr '\0' '\n' < "$d/environ"; } 2>/dev/null | grep -q "^STEAM_COMPAT_DATA_PATH=$COMPAT\$" && echo "${d#/proc/}"
    done
}
game_running() { pgrep -u "$USER" -f 'Titanfall2\.exe' >/dev/null; }
tracesize() { du -sbL "$LOGDIR" "$GAME"/Titanfall2*.trace 2>/dev/null | awk '{s+=$1} END {print s+0}'; }
same() { [ "$(sha256sum < "$1" | cut -d' ' -f1)" = "$(sha256sum < "$2" | cut -d' ' -f1)" ]; }
FINISHED=0
finish() {
    [ "$FINISHED" = 1 ] && return; FINISHED=1
    say "finish: $1"
    for p in $(pgrep -u "$USER" -f 'Titanfall2\.exe'); do kill -TERM "$p" 2>/dev/null; done
    N=0; while [ -n "$(game_pids)" ] && [ "$N" -lt 20 ]; do sleep 5; N=$((N + 5)); done
    for p in $(game_pids); do kill -TERM "$p" 2>/dev/null; done
    N=0; while [ -n "$(game_pids)" ] && [ "$N" -lt 20 ]; do sleep 5; N=$((N + 5)); done
    for p in $(game_pids); do kill -KILL "$p" 2>/dev/null; done
    sleep 2
    rm -f "$US"; rm -f "$PROTON"/__pycache__/user_settings.*.pyc
    say "user_settings.py removed: $([ -e "$US" ] && echo NO || echo yes); prefix processes left: $(game_pids | wc -l)"
    for f in d3d11.dll dxgi.dll dxgitrace.dll; do
        if [ -f "$BIN/$f" ] && same "$BIN/$f" "$W/$f"; then rm -f "$BIN/$f"; fi
        [ -e "$BIN/$f" ] && say "WRAPPER NOT REMOVED: $BIN/$f (it is not identical to ours; look at it)"
    done
    say "wrappers left in the game's directory: $(ls "$BIN" | grep -c -i -x -E 'd3d11\.dll|dxgi\.dll|dxgitrace\.dll')"
    for f in "$GAME"/Titanfall2*.trace; do
        [ -f "$f" ] || continue
        mkdir -p "$LIB/tf2-trace-apitrace/$LABEL" && mv "$f" "$LIB/tf2-trace-apitrace/$LABEL/" \
            && ln -s "$LIB/tf2-trace-apitrace/$LABEL/$(basename "$f")" "$LOGDIR/" \
            && say "trace found in the game's directory, moved to $LIB/tf2-trace-apitrace/$LABEL/ and linked"
    done
    { echo "--- files:"; ls -laL "$LOGDIR"; } >> "$ST" 2>&1
    say "DONE"
}
trap 'finish "interrupted"; exit 0' INT TERM HUP

: > "$ST"
case "$MODE" in
    auto:*) SECS=${MODE#auto:}; LAUNCH=1;;
    watch) SECS=0; LAUNCH=0;;
    *) say "ABORT: mode $MODE"; say "DONE"; exit 1;;
esac
abort() { say "ABORT: $1"; say "DONE"; exit 1; }
[ -e "$LOGDIR" ] && abort "$LOGDIR exists; choose a new label"
[ -n "$(game_pids)" ] && abort "the game's prefix already has running processes (close the game and the EA app first)"
[ -e "$US" ] && abort "user_settings.py already exists; not mine to overwrite"
pgrep -u "$USER" -x steam >/dev/null || abort "the Steam client is not running"
[ -e "$T/relaykey.state.json" ] && abort "relay values are still set in the prefix (relaykey.py restore first)"
for f in d3d11.dll dxgi.dll dxgitrace.dll; do
    [ -f "$W/$f" ] || abort "no wrapper $W/$f"
    [ -e "$BIN/$f" ] && abort "$BIN/$f already exists; not mine to overwrite"
done
ls "$GAME"/Titanfall2*.trace >/dev/null 2>&1 && abort "a .trace file is already in the game's directory"
(cd "$W" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || abort "the wrappers do not match their recorded checksums"
mkdir -p "$LOGDIR"
for f in d3d11.dll dxgi.dll dxgitrace.dll; do cp "$W/$f" "$BIN/$f" || { finish "could not copy $f"; exit 1; }; done
say "wrappers copied into $BIN"
cat > "$US" <<PY
# Written by ~/tf2-trace/apitrace_run.sh for one traced run; removed when the run ends.
user_settings = {
    "PROTON_LOG": "1",
    "PROTON_LOG_DIR": "$LOGDIR",
    "WINEDEBUG": "$CHANNELS",
    "DXVK_LOG_LEVEL": "info",
    "DXVK_LOG_PATH": "$LOGDIR",
    "TRACE_FILE": "Z:$LOGDIR/Titanfall2.trace",
}
PY
say "logging on; channels: $CHANNELS; TRACE_FILE=Z:$LOGDIR/Titanfall2.trace"

if [ "$LAUNCH" = 1 ]; then
    SP=$(pgrep -u "$USER" -x steam | head -1)
    while IFS= read -r -d '' kv; do
        case "$kv" in DISPLAY=*|WAYLAND_DISPLAY=*|XDG_RUNTIME_DIR=*|DBUS_SESSION_BUS_ADDRESS=*|XAUTHORITY=*) export "$kv";; esac
    done < /proc/$SP/environ
    timeout 30 "$HOME/.local/share/Steam/steam.sh" steam://rungameid/1237970 >> "$LOGDIR/steam-launch.out" 2>&1
    say "launch requested (steam.sh exit $?)"
else
    say "READY: start the game from Steam now"
fi

N=0
until game_running; do
    sleep 3; N=$((N + 3))
    [ $((N % 30)) = 0 ] && say "waiting ${N}s for the game's process; prefix processes: $(game_pids | wc -l)"
    if [ "$N" -ge "$MAX_WAIT_SECS" ]; then finish "gave up waiting after ${N}s"; exit 0; fi
done
START=$(date +%s); say "GAME SEEN after ${N}s"
GONE=0
while :; do
    sleep 5; NOW=$(( $(date +%s) - START )); SZ=$(tracesize)
    [ $((NOW % 30)) -lt 5 ] && say "t=${NOW}s bytes=$SZ prefix_procs=$(game_pids | wc -l)"
    if game_running; then GONE=0; else GONE=$((GONE + 5)); fi
    [ "$GONE" -ge 20 ] && { finish "the game has exited (t=${NOW}s, bytes=$SZ)"; break; }
    [ "$SZ" -gt "$MAX_BYTES" ] && { finish "more than 40 GB written (t=${NOW}s)"; break; }
    [ "$SECS" != 0 ] && [ "$NOW" -ge "$SECS" ] && { finish "test time reached (t=${NOW}s, bytes=$SZ)"; break; }
    [ "$NOW" -ge "$MAX_RUN_SECS" ] && { finish "25 minutes reached"; break; }
done
exit 0
