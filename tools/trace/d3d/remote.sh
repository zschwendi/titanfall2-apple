#!/bin/sh
# remote.sh: run d3dtrace.py over the Direct3D call capture ON THE MACHINE THAT HOLDS IT, so
# that only the tool's output comes back. Read-only there: one `apitrace dump` piped into
# `python3 -c <this repository's d3dtrace.py>`; nothing is written or left running.
#
#   tools/trace/d3d/remote.sh [-t mission1|test1] [-c A-B] [-v] SUBCOMMAND [ARGS]
#
#   remote.sh summary > build/trace/summary.json            the whole session counted (about 2 minutes)
#   remote.sh -c 8778-8900 clean --cut 400 > build/trace/x.txt   call lines of a range, shader text removed
#   remote.sh -c 0-9000 start                                the start, folded
#   remote.sh -c A-B frame                                   one frame, folded (A and B from the summary's
#                                                            per_frame.first_call)
#
# -v passes --verbose to the dump: it then lists the calls it otherwise leaves out (the ones
# the recorder marked as having no side effect: CheckFormatSupport, CheckMultisampleQualityLevels
# and the like; finding 40's correction of 11 October), with the real device's answers. The
# pipe is the same: every line still goes through d3dtrace.py's call_lines() there, which is
# what takes the shader text out, so nothing more crosses than without it (the planner's
# answer B to gate3-run-4).
#
# NEVER run `apitrace dump` there without this pipe: the dump prints the game's shaders as text
# (see the head of d3dtrace.py). One command at a time; the dump alone takes about 10 seconds.
set -eu
# The reference PC: $D3DTRACE_HOST, $TF_REF_HOST, or host=... in .ref-host at the checkout's root (git-ignored).
HOST=${D3DTRACE_HOST:-${TF_REF_HOST:-$(sed -n 's/^host=//p' "$(dirname "$0")/../../../.ref-host" 2>/dev/null | head -1)}}
[ -n "$HOST" ] || { echo 'remote.sh: no reference PC: put host=user@host in .ref-host (or set TF_REF_HOST)' >&2; exit 2; }
BIN='~/tf2-trace/apitrace/linux/apitrace-14.0-Linux/bin/apitrace'
which=mission1
calls=
verbose=
while [ $# -gt 0 ]; do
    case "$1" in
        -t) which=$2; shift 2 ;;
        -c) calls="--calls=$2"; shift 2 ;;
        -v) verbose=--verbose; shift ;;
        *) break ;;
    esac
done
case "$which" in mission1|test1) ;; *) echo "remote.sh: -t mission1 or test1" >&2; exit 2 ;; esac
case "$calls" in ''|--calls=[0-9]*-[0-9]*) ;; *) echo "remote.sh: -c A-B" >&2; exit 2 ;; esac
[ $# -gt 0 ] || { sed -n '2,15p' "$0" >&2; exit 2; }
case "$1" in clean|summary|start|frame) ;; *) echo "remote.sh: clean, summary, start or frame" >&2; exit 2 ;; esac
args=
for a in "$@"; do
    case "$a" in *\'*|*\"*|*\\*|*\$*|*\`*) echo "remote.sh: no quotes, backslashes or \$ in arguments" >&2; exit 2 ;; esac
    args="$args '$a'"
done
here=$(dirname "$0")
code=$(python3 -c 'import sys,zlib,base64
sys.stdout.write(base64.b64encode(zlib.compress(open(sys.argv[1],"rb").read(),9)).decode())' "$here/d3dtrace.py")
exec ssh -o BatchMode=yes -o ConnectTimeout=15 "$HOST" \
    "nice $BIN dump --color=never $verbose $calls ~/tf2-trace/a3-apitrace-$which/Titanfall2.trace 2>/dev/null | nice python3 -c \"import sys,zlib,base64;exec(zlib.decompress(base64.b64decode('$code')).decode())\" $args"
