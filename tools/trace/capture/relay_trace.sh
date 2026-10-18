#!/bin/bash
# One relay-traced run of Titanfall 2 under Proton: the Windows calls made from the game's
# own DLLs (the modules in ~/tf2-trace/from.list; OriginSDK.dll is deliberately not one).
# usage: relay_trace.sh <label> auto:<secs>|watch|watch:<secs> [<exclude-list-file>]
#   auto:<secs>   start the game through the running Steam client and stop it <secs> seconds
#                 after the game's process appears (an unattended test)
#   watch         wait for the owner to start the game himself, record until it exits
#   watch:<secs>  wait for a start that is already on its way (Steam may first process
#                 shaders for minutes), stop the game <secs> seconds after it appears
# What it changes outside ~/tf2-trace, and puts back when the run ends:
#   user_settings.py in Proton Experimental (logging on), and the Relay* values of
#   [Software\\Wine\\Debug] in the game prefix's user.reg (relaykey.py; a copy of user.reg is
#   kept under reg-backup/ first).  It refuses to start if either is in an unexpected state.
# Everything else it writes is ~/tf2-trace/<label>/ and ~/tf2-trace/status-<label>.txt.
# Two optional settings from the environment: CHANNELS_EXTRA (for example +snoop) and
# KEYS_EXTRA (further values for relaykey.py, for example SnoopInclude=<file>).
set -u
LABEL=${1:?label}; MODE=${2:?auto:<secs> or watch}; EXCL=${3:-}
T=$HOME/tf2-trace
PROTON="$HOME/.local/share/Steam/steamapps/common/Proton - Experimental"
COMPAT=/mnt/1tb_nvme/SteamLibrary/steamapps/compatdata/1237970
US="$PROTON/user_settings.py"
ST=$T/status-$LABEL.txt
LOGDIR=$T/$LABEL
CHANNELS=+timestamp,+pid,+tid,+relay,+loaddll,+seh,+process,+threadname,+debugstr${CHANNELS_EXTRA:+,$CHANNELS_EXTRA}
MAX_LOG_BYTES=$((10 * 1024 * 1024 * 1024)); MAX_RUN_SECS=1800; MAX_WAIT_SECS=1800
say() { echo "$(date +%H:%M:%S) $*" >> "$ST"; }
game_pids() {   # every process started for this game's prefix (never the Steam client itself)
    for d in /proc/[0-9]*; do
        { tr '\0' '\n' < "$d/environ"; } 2>/dev/null | grep -q "^STEAM_COMPAT_DATA_PATH=$COMPAT\$" && echo "${d#/proc/}"
    done
}
game_running() { pgrep -u "$USER" -f 'Titanfall2\.exe' >/dev/null; }
logsize() { du -sb "$LOGDIR" 2>/dev/null | awk '{s+=$1} END {print s+0}'; }
FINISHED=0
finish() {      # stop what is left of the prefix, put both settings back, first look at the log
    [ "$FINISHED" = 1 ] && return; FINISHED=1
    say "finish: $1"
    for p in $(pgrep -u "$USER" -f 'Titanfall2\.exe'); do kill -TERM "$p" 2>/dev/null; done
    W=0; while [ -n "$(game_pids)" ] && [ "$W" -lt 20 ]; do sleep 5; W=$((W + 5)); done
    for p in $(game_pids); do kill -TERM "$p" 2>/dev/null; done
    W=0; while [ -n "$(game_pids)" ] && [ "$W" -lt 20 ]; do sleep 5; W=$((W + 5)); done
    for p in $(game_pids); do kill -KILL "$p" 2>/dev/null; done
    sleep 2
    rm -f "$US"; rm -f "$PROTON"/__pycache__/user_settings.*.pyc
    say "user_settings.py removed: $([ -e "$US" ] && echo NO || echo yes); prefix processes left: $(game_pids | wc -l)"
    if [ -z "$(game_pids)" ]; then say "$(python3 "$T/relaykey.py" restore 2>&1)"
    else say "REGISTRY NOT RESTORED: processes still run in the prefix; run 'python3 ~/tf2-trace/relaykey.py restore' when they are gone"; fi
    { echo "--- files:"; ls -la "$LOGDIR"; } >> "$ST" 2>&1
    L=$LOGDIR/steam-1237970.log
    [ -f "$L" ] && nice python3 "$T/relay_hist.py" "$L" > "$LOGDIR/hist.txt" 2>&1 && say "first look written: $LOGDIR/hist.txt"
    say "DONE"
}
trap 'finish "interrupted"; exit 0' INT TERM HUP

: > "$ST"
case "$MODE" in
    auto:*) SECS=${MODE#auto:}; LAUNCH=1;;
    watch:*) SECS=${MODE#watch:}; LAUNCH=0;;
    watch) SECS=0; LAUNCH=0;;
    *) say "ABORT: mode $MODE"; say "DONE"; exit 1;;
esac
if [ -e "$LOGDIR" ]; then say "ABORT: $LOGDIR exists; choose a new label (earlier captures are kept)"; say "DONE"; exit 1; fi
if [ -n "$(game_pids)" ]; then say "ABORT: the game's prefix already has running processes (close the game and the EA app first)"; say "DONE"; exit 1; fi
if [ -e "$US" ]; then say "ABORT: user_settings.py already exists; not mine to overwrite"; say "DONE"; exit 1; fi
if ! pgrep -u "$USER" -x steam >/dev/null; then say "ABORT: the Steam client is not running"; say "DONE"; exit 1; fi
if [ -n "$EXCL" ] && [ ! -f "$EXCL" ]; then say "ABORT: no exclude list $EXCL"; say "DONE"; exit 1; fi
R=$(python3 "$T/relaykey.py" set "$T/from.list" "$EXCL" ${KEYS_EXTRA:-} 2>&1) || { say "ABORT: $R"; say "DONE"; exit 1; }
say "$R"
mkdir -p "$LOGDIR"
cp "$T/from.list" "$LOGDIR/from.list"; [ -n "$EXCL" ] && cp "$EXCL" "$LOGDIR/exclude.list"
cat > "$US" <<PY
# Written by ~/tf2-trace/relay_trace.sh for one traced run; removed when the run ends.
user_settings = {
    "PROTON_LOG": "1",
    "PROTON_LOG_DIR": "$LOGDIR",
    "WINEDEBUG": "$CHANNELS",
    "DXVK_LOG_LEVEL": "info",
    "DXVK_LOG_PATH": "$LOGDIR",
}
PY
say "logging on; channels: $CHANNELS; exclude list: ${EXCL:-prefix default}; further values: ${KEYS_EXTRA:-none}"

if [ "$LAUNCH" = 1 ]; then
    # The session environment of the running Steam client, so the request reaches it.
    SP=$(pgrep -u "$USER" -x steam | head -1)
    while IFS= read -r -d '' kv; do
        case "$kv" in DISPLAY=*|WAYLAND_DISPLAY=*|XDG_RUNTIME_DIR=*|DBUS_SESSION_BUS_ADDRESS=*|XAUTHORITY=*) export "$kv";; esac
    done < /proc/$SP/environ
    timeout 30 "$HOME/.local/share/Steam/steam.sh" steam://rungameid/1237970 >> "$LOGDIR/steam-launch.out" 2>&1
    say "launch requested (steam.sh exit $?)"
else
    say "READY: start the game from Steam now"
fi

W=0
until game_running; do
    sleep 3; W=$((W + 3))
    [ $((W % 30)) = 0 ] && say "waiting ${W}s for the game's process; prefix processes: $(game_pids | wc -l); log_bytes=$(logsize)"
    if [ "$LAUNCH" = 1 ] && [ "$W" -ge 240 ]; then finish "no game process after ${W}s"; exit 0; fi
    if [ "$W" -ge "$MAX_WAIT_SECS" ]; then finish "gave up waiting after ${W}s"; exit 0; fi
    if [ "$(logsize)" -gt "$MAX_LOG_BYTES" ]; then finish "log larger than 10 GB before the game appeared"; exit 0; fi
done
START=$(date +%s); say "GAME SEEN after ${W}s; log_bytes=$(logsize)"
GONE=0
while :; do
    sleep 5; NOW=$(( $(date +%s) - START )); SZ=$(logsize)
    [ $((NOW % 30)) -lt 5 ] && say "t=${NOW}s log_bytes=$SZ prefix_procs=$(game_pids | wc -l)"
    if game_running; then GONE=0; else GONE=$((GONE + 5)); fi
    [ "$GONE" -ge 20 ] && { finish "the game has exited (t=${NOW}s, log_bytes=$SZ)"; break; }
    [ "$SZ" -gt "$MAX_LOG_BYTES" ] && { finish "log larger than 10 GB (t=${NOW}s)"; break; }
    [ "$SECS" != 0 ] && [ "$NOW" -ge "$SECS" ] && { finish "test time reached (t=${NOW}s, log_bytes=$SZ)"; break; }
    [ "$NOW" -ge "$MAX_RUN_SECS" ] && { finish "30 minutes reached"; break; }
done
exit 0
