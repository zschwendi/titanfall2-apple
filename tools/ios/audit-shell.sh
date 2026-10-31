#!/bin/sh
# audit-shell.sh APP: fail unless APP is a shell with no game code in it. Checks every Mach-O file for the
# recompiled modules' symbols (named after gate3/modules.def), the generated module table, the runtime
# (which lives in the payload), and the payload framework itself; prints one line per finding.
set -eu
cd "$(dirname "$0")/../.."
app=${1:?usage: audit-shell.sh APP}
[ -d "$app" ] || { echo "not a directory: $app" >&2; exit 2; }
fail=0
[ ! -e "$app/Frameworks/TF2Game.framework" ] || { echo "payload present: Frameworks/TF2Game.framework"; fail=1; }
modules=$(sed -n 's/^lifted[[:space:]]*\([^.]*\)\.dll.*/\1/p' gate3/modules.def | tr -d '()' | paste -sd'|' -)
[ -n "$modules" ] || { echo "no modules read from gate3/modules.def" >&2; exit 2; }
files=0
for f in $(find "$app" -type f); do
    file -b "$f" | grep -q 'Mach-O' || continue
    files=$((files + 1))
    # A stripped binary hides its symbols; the recompiled game alone is over 200 MB, the shell's largest file 25 MB.
    if [ "$(stat -f %z "$f")" -gt 67108864 ]; then echo "larger than 64 MB: ${f#$app/}"; fail=1; fi
    syms=$(nm -a "$f" 2>/dev/null || true)
    if printf '%s\n' "$syms" | grep -q -E " _($modules)_[0-9a-fA-F]"; then echo "recompiled game code: ${f#$app/}"; fail=1; fi
    if printf '%s\n' "$syms" | grep -q -E ' _(gate3_module_defs|g_base_[A-Za-z0-9_]+)$'; then echo "game module table: ${f#$app/}"; fail=1; fi
    if printf '%s\n' "$syms" | grep -q -E ' _(rt_dispatch_fn|gate3_run_embedded)$'; then echo "runtime or driver (belongs in the payload): ${f#$app/}"; fail=1; fi
done
[ "$files" -gt 0 ] || { echo "no Mach-O files found in $app" >&2; exit 2; }
if [ "$fail" = 0 ]; then echo "audit-shell: $files Mach-O files, no game code, table, runtime or payload"; fi
exit "$fail"
