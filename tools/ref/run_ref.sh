#!/usr/bin/env bash
# run_ref.sh <game install directory> <scratch directory>
#
# WHAT THIS DOES, IN PLAIN WORDS
#
#   It runs one small test program of ours, refcalls.exe (it must lie beside this script),
#   under the Wine that the installed "Proton - Experimental" contains.  The program loads the
#   game's tier0.dll and vstdlib.dll straight from the install directory and calls their
#   exported functions: first the 90 ranks of our driver's call list (rank 29 is left out),
#   then the game's own queue self-test 400 times.  Loading tier0.dll also loads
#   OriginSDK.dll, which it imports; nothing in that DLL is called.
#
#   It does NOT start Steam, the Steam client, Origin, or the game.  It does not use or touch
#   the game's own Wine prefix (the one with your account data): it makes a fresh, throwaway
#   prefix inside the scratch directory and deletes it again at the end.  It opens no window
#   (the display variables are removed for Wine) and uses no network.
#
#   It reads: the two DLLs (to load them, and for their size and SHA-256), the Proton
#   directory, /proc/cpuinfo.  It changes nothing in the install directory or in Steam's.
#   It writes only under the scratch directory:
#       results/calls.txt   one line per rank: what the real DLLs returned
#       results/queue.txt   one line per run of the queue self-test, then the counts
#       results/env.txt     kernel, CPU model, Proton and Wine version, the DLLs' size and SHA-256
#       results/*.stderr.txt  what Wine itself said (normally little)
#       prefix/, work/      the throwaway Wine prefix and temporary files: removed at the end
#   The prefix is the bulk (several hundred megabytes while it exists); results/ is under 1 MB.
#
#   It refuses to run if the scratch directory is under /mnt/2tb_hdd, if it is not empty and
#   was not made by an earlier run of this script, or if less than 3 GB are free there.
#   It is safe to interrupt (Ctrl-C, power loss) and to run again: a new run removes what an
#   interrupted one left in prefix/ and work/ and writes the results afresh.
#
#   Time: a few minutes for the prefix, a few for the ranks, about ten for the 400 queue
#   runs.  Everything together is stopped after REF_TIME_LIMIT seconds (default 10800, 3 h);
#   what was measured until then stays in results/.
#
# Settings (environment), all optional:
#   PROTON_DIR=<dir>      the Proton directory, if it is not found next to the game
#   REF_RUNS=<n>          queue self-test runs (400)
#   REF_TIME_LIMIT=<s>    overall time limit (10800)
#   REF_SYNC=server       do not ask Wine for esync/fsync (default: ask for both, as Proton does)
#
# Example:
#   ./run_ref.sh "$HOME/.steam/steam/steamapps/common/Titanfall2" "$HOME/ref-scratch"

set -u

TAB=$(printf '\t')
say() { printf 'run_ref: %s\n' "$*"; }
die() { printf 'run_ref: STOPPED: %s\n' "$*" >&2; exit 1; }

[ "$#" -eq 2 ] || die "usage: run_ref.sh <game install directory> <scratch directory>   (the first lines of this script say what it does)"
[ "$(uname -s)" = "Linux" ] || die "this script is for the Linux machine that has the game and Proton"
for tool in realpath sha256sum stat df timeout du find; do
    command -v "$tool" > /dev/null 2>&1 || die "the command '$tool' is missing"
done

HERE=$(cd "$(dirname "$0")" && pwd -P) || die "cannot find this script's own directory"
EXE="$HERE/refcalls.exe"
[ -f "$EXE" ] || die "refcalls.exe is not beside this script (looked for $EXE)"

INSTALL=$(realpath -e -- "$1" 2> /dev/null) || die "the install directory '$1' does not exist"
SCRATCH=$(realpath -m -- "$2") || die "cannot make sense of the scratch directory '$2'"

# ---- the scratch directory: where it may not be ---------------------------------------------------
case "$SCRATCH/" in
    /mnt/2tb_hdd/*) die "the scratch directory is under /mnt/2tb_hdd; that drive is not to be written to. Choose another place" ;;
esac
case "$2" in
    /mnt/2tb_hdd | /mnt/2tb_hdd/*) die "the scratch directory is under /mnt/2tb_hdd; that drive is not to be written to. Choose another place" ;;
esac
[ "$SCRATCH" != "/" ] && [ "$SCRATCH" != "${HOME:-/nonexistent}" ] || die "the scratch directory must be a directory of its own, not / or your home"
case "$SCRATCH/" in
    "$INSTALL"/*) die "the scratch directory must not be inside the game's install directory" ;;
esac
case "$INSTALL/" in
    "$SCRATCH"/*) die "the game's install directory must not be inside the scratch directory" ;;
esac
MARK="$SCRATCH/.run_ref_scratch"
if [ -e "$SCRATCH" ]; then
    [ -d "$SCRATCH" ] || die "'$SCRATCH' exists and is not a directory"
    if [ ! -e "$MARK" ] && [ -n "$(find "$SCRATCH" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
        die "'$SCRATCH' is not empty and was not made by this script; give an empty or new directory"
    fi
fi

# ---- the two DLLs -----------------------------------------------------------------------------------
if [ -f "$INSTALL/bin/x64_retail/tier0.dll" ]; then DLLDIR="$INSTALL/bin/x64_retail"
elif [ -f "$INSTALL/tier0.dll" ]; then DLLDIR="$INSTALL"
else die "no tier0.dll in '$INSTALL/bin/x64_retail' or in '$INSTALL': is this the game's install directory?"
fi
[ -f "$DLLDIR/vstdlib.dll" ] || die "no vstdlib.dll in '$DLLDIR'"
case "$DLLDIR" in
    *\"*) die "the path of the DLLs contains a double quote; the program cannot be given such a path" ;;
esac

# ---- Proton - Experimental ---------------------------------------------------------------------------
find_proton() {
    local c lib root vdf
    if [ -n "${PROTON_DIR:-}" ]; then printf '%s\n' "$PROTON_DIR"; return 0; fi
    # next to the game: <library>/steamapps/common/<game>  ->  <library>/steamapps/common/Proton - Experimental
    c=$INSTALL
    while [ "$c" != "/" ]; do
        if [ "$(basename "$c")" = "common" ] && [ -d "$c/Proton - Experimental" ]; then printf '%s\n' "$c/Proton - Experimental"; return 0; fi
        c=$(dirname "$c")
    done
    for root in "$HOME/.steam/steam" "$HOME/.steam/root" "$HOME/.local/share/Steam" "$HOME/snap/steam/common/.local/share/Steam" \
                "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
        [ -d "$root/steamapps" ] || continue
        if [ -d "$root/steamapps/common/Proton - Experimental" ]; then printf '%s\n' "$root/steamapps/common/Proton - Experimental"; return 0; fi
        vdf="$root/steamapps/libraryfolders.vdf"
        [ -f "$vdf" ] || continue
        while IFS= read -r lib; do
            if [ -d "$lib/steamapps/common/Proton - Experimental" ]; then printf '%s\n' "$lib/steamapps/common/Proton - Experimental"; return 0; fi
        done < <(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' "$vdf")
    done
    return 1
}
PROTON=$(find_proton) || die "\"Proton - Experimental\" was not found next to the game or in the usual Steam places; set PROTON_DIR=<its directory> and run again"
[ -d "$PROTON" ] || die "PROTON_DIR='$PROTON' is not a directory"
WINE=""
for cand in "$PROTON/files/bin/wine64" "$PROTON/files/bin/wine" "$PROTON/dist/bin/wine64" "$PROTON/dist/bin/wine"; do
    if [ -x "$cand" ]; then WINE=$cand; break; fi
done
[ -n "$WINE" ] || die "no wine or wine64 program under '$PROTON/files/bin' (or dist/bin): is this a Proton directory?"
WINEBIN=$(dirname "$WINE")
WINESERVER="$WINEBIN/wineserver"
[ -x "$WINESERVER" ] || die "no wineserver beside '$WINE'"
PROTON_FILES=$(dirname "$WINEBIN")

# ---- room, and what an interrupted run left behind ----------------------------------------------------
mkdir -p "$SCRATCH" || die "cannot create '$SCRATCH'"
: > "$MARK" || die "cannot write in '$SCRATCH'"
FREE_MB=$(df -Pm "$SCRATCH" | awk 'NR == 2 { print $4 }')
case "$FREE_MB" in
    '' | *[!0-9]*) die "cannot read the free space of '$SCRATCH'" ;;
esac
[ "$FREE_MB" -ge 3000 ] || die "only $FREE_MB MB free in '$SCRATCH'; 3000 are asked for (the throwaway prefix needs several hundred)"

PREFIX="$SCRATCH/prefix"
WORK="$SCRATCH/work"
RESULTS="$SCRATCH/results"

export WINEPREFIX="$PREFIX"
export WINEARCH=win64
export WINEDEBUG=fixme-all
export WINEDLLOVERRIDES="winedbg.exe=d;mscoree=d;mshtml=d;winemenubuilder.exe=d"
export TZ=UTC
export LD_LIBRARY_PATH="$PROTON_FILES/lib64:$PROTON_FILES/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
unset DISPLAY WAYLAND_DISPLAY WINELOADER WINEDLLPATH SteamAppId SteamGameId STEAM_COMPAT_DATA_PATH
if [ "${REF_SYNC:-}" = "server" ]; then unset WINEESYNC WINEFSYNC; else export WINEESYNC=1 WINEFSYNC=1; fi

stop_wine() {       # end every process of OUR prefix (never another one: WINEPREFIX is set above)
    if [ -d "$PREFIX" ]; then
        timeout 60 "$WINESERVER" -k > /dev/null 2>&1
        timeout 60 "$WINESERVER" -w > /dev/null 2>&1
    fi
    return 0
}
remove_temporaries() {
    stop_wine
    rm -rf -- "$PREFIX" "$WORK"
}
finish() {
    trap - EXIT INT TERM HUP
    remove_temporaries
}
trap 'finish' EXIT
trap 'say "interrupted: removing the prefix; what was measured so far is in $RESULTS (a file ending in .part is the mode that was running)"; exit 130' INT TERM HUP

if [ -e "$PREFIX" ] || [ -e "$WORK" ]; then say "removing what an interrupted run left in $PREFIX and $WORK"; fi
remove_temporaries
mkdir -p "$WORK" "$RESULTS" || die "cannot create directories under '$SCRATCH'"
rm -f -- "$RESULTS/calls.txt" "$RESULTS/queue.txt" "$RESULTS/env.txt" "$RESULTS"/*.part "$RESULTS"/*.stderr.txt
cp -- "$EXE" "$WORK/refcalls.exe" || die "cannot copy refcalls.exe into '$WORK'"

START=$(date +%s)
LIMIT=${REF_TIME_LIMIT:-10800}
RUNS=${REF_RUNS:-400}
case "$LIMIT" in '' | *[!0-9]*) die "REF_TIME_LIMIT must be a number of seconds" ;; esac
case "$RUNS" in '' | *[!0-9]*) die "REF_RUNS must be a number" ;; esac
left() {            # seconds left of the overall limit, at least 1
    local l=$(( START + LIMIT - $(date +%s) ))
    [ "$l" -ge 1 ] || l=1
    printf '%s\n' "$l"
}

# ---- results/env.txt -----------------------------------------------------------------------------------
ENV="$RESULTS/env.txt"
{
    printf 'date_utc\t%s\n' "$(date -u '+%Y-%m-%d %H:%M:%S')"
    printf 'kernel\t%s\n' "$(uname -srvm)"
    if [ -r /etc/os-release ]; then printf 'system\t%s\n' "$(sed -n 's/^PRETTY_NAME="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' /etc/os-release | head -n 1)"; fi
    printf 'cpu_model\t%s\n' "$(sed -n 's/^model name[[:space:]]*:[[:space:]]*//p' /proc/cpuinfo | head -n 1)"
    printf 'cpu_logical\t%s\n' "$(grep -c '^processor' /proc/cpuinfo)"
    printf 'proton_dir\t%s\n' "$PROTON"
    if [ -r "$PROTON/version" ]; then printf 'proton_version\t%s\n' "$(head -n 1 "$PROTON/version")"; else printf 'proton_version\t(no version file)\n'; fi
    printf 'wine_program\t%s\n' "$WINE"
    printf 'sync_asked\t%s\n' "$([ "${REF_SYNC:-}" = "server" ] && echo "server (REF_SYNC=server)" || echo "WINEESYNC=1 WINEFSYNC=1, as Proton asks; Wine falls back by itself where the kernel lacks one")"
    printf 'tz\tUTC (set for Wine by this script)\n'
    printf 'dll_dir\t%s\n' "$DLLDIR"
    for f in tier0.dll vstdlib.dll; do
        printf 'dll\t%s\t%s bytes\tsha256 %s\n' "$f" "$(stat -c %s "$DLLDIR/$f")" "$(sha256sum "$DLLDIR/$f" | cut -d' ' -f1)"
    done
    printf 'program\trefcalls.exe\t%s bytes\tsha256 %s\n' "$(stat -c %s "$EXE")" "$(sha256sum "$EXE" | cut -d' ' -f1)"
    printf 'queue_runs_asked\t%s\n' "$RUNS"
    printf 'time_limit_s\t%s\n' "$LIMIT"
} > "$ENV"

# ---- the throwaway prefix ------------------------------------------------------------------------------
cd "$WORK" || die "cannot enter '$WORK'"
say "making a fresh Wine prefix in $PREFIX with $WINE (a few minutes; no window will open)"
BOOT_LIMIT=$(left)
[ "$BOOT_LIMIT" -le 900 ] || BOOT_LIMIT=900
timeout --kill-after=30 "$BOOT_LIMIT" "$WINE" wineboot --init > "$RESULTS/wineboot.stderr.txt" 2>&1
BOOT=$?
timeout 300 "$WINESERVER" -w > /dev/null 2>&1
if [ ! -f "$PREFIX/system.reg" ]; then
    printf 'outcome\tSTOPPED: the Wine prefix could not be made (wineboot ended with %s)\n' "$BOOT" >> "$ENV"
    tail -n 15 "$RESULTS/wineboot.stderr.txt" >&2
    die "Wine did not make the prefix (exit status $BOOT); its last words are above and in $RESULTS/wineboot.stderr.txt"
fi
printf 'wine_version\t%s\n' "$(timeout 120 "$WINE" --version 2> /dev/null | head -n 1)" >> "$ENV"

# Does the program start at all?  Without arguments it prints its usage.
timeout --kill-after=30 120 "$WINE" "$WORK/refcalls.exe" > "$WORK/usage.out" 2> "$RESULTS/start.stderr.txt"
if ! grep -q '^usage: refcalls.exe' "$WORK/usage.out"; then
    printf 'outcome\tSTOPPED: refcalls.exe did not start under Wine\n' >> "$ENV"
    tail -n 15 "$RESULTS/start.stderr.txt" >&2
    die "refcalls.exe did not print its usage under Wine; Wine's words are above and in $RESULTS/start.stderr.txt"
fi

# The program is given the DLLs' directory as Windows sees it: Wine's drive Z: is the Linux root.
DLLWIN="Z:$(printf '%s' "$DLLDIR" | tr '/' '\\')"

run_mode() {        # run_mode <name> <arguments of refcalls.exe after the directory ...>
    local name=$1 status
    shift
    say "$name: started $(date '+%H:%M:%S'), at most $(left) s"
    timeout --kill-after=30 "$(left)" "$WINE" "$WORK/refcalls.exe" "$name" "$DLLWIN" "$@" > "$RESULTS/$name.txt.part" 2> "$RESULTS/$name.stderr.txt"
    status=$?
    stop_wine
    mv -f -- "$RESULTS/$name.txt.part" "$RESULTS/$name.txt"
    if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
        printf '# run_ref.sh: stopped by the overall time limit; the lines above are what was measured until then\n' >> "$RESULTS/$name.txt"
        printf 'outcome_%s\tstopped by the time limit\n' "$name" >> "$ENV"
        say "$name: STOPPED BY THE TIME LIMIT; partial results kept"
    elif ! grep -q "^S${TAB}${name}${TAB}" "$RESULTS/$name.txt"; then
        printf '# run_ref.sh: the program ended with status %s before its last line; the lines above are what it wrote\n' "$status" >> "$RESULTS/$name.txt"
        printf 'outcome_%s\tincomplete: the program ended with status %s and no summary line\n' "$name" "$status" >> "$ENV"
        say "$name: INCOMPLETE (status $status); see $RESULTS/$name.txt and $RESULTS/$name.stderr.txt"
    else
        printf 'outcome_%s\tcomplete\n' "$name" >> "$ENV"
        say "$name: $(grep "^S${TAB}${name}${TAB}" "$RESULTS/$name.txt" | tr '\t' ' ')"
    fi
}

run_mode calls
run_mode queue --runs "$RUNS"

printf 'prefix_size_mb\t%s\n' "$(du -sm "$PREFIX" 2> /dev/null | cut -f1)" >> "$ENV"
printf 'elapsed_s\t%s\n' "$(( $(date +%s) - START ))" >> "$ENV"
finish
if [ -e "$PREFIX" ] || [ -e "$WORK" ]; then say "NOTE: $PREFIX or $WORK could not be removed; delete them by hand"; fi
say "done. The results are in $RESULTS (calls.txt, queue.txt, env.txt): copy that directory back."
exit 0
