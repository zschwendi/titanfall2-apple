#!/bin/sh
# Run the game; when it stops on game data that is not on this Mac, fetch it and run again.
# The loop is tools/gamedata/run_fetch.py (its first lines say what it does and how it ends).
#
#     tools/gamedata/run_fetch.sh [--rounds N] [--floor-gb G] [--whole-max-gb G] [--game DIR] [-- game arguments]
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
py="$root/.venv/bin/python"
[ -x "$py" ] || py=python3
exec "$py" "$here/run_fetch.py" "$@"
