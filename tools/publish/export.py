#!/usr/bin/env python3
"""Copy the allowlisted tracked files into a new tree, then run the guard on it.

Usage: export.py --out DIR [--allowlist tools/publish/allowlist.txt]
DIR must not exist. The allowlist has one fnmatch glob per line; '#' starts a
comment; a line starting with '!' excludes. The last matching line wins.
A file that no line matches is left out. Exits with check.py's status.
"""
import fnmatch
import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def read_allowlist(path):
    rules = []
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("!"):
            rules.append((False, line[1:].strip()))
        else:
            rules.append((True, line))
    return rules


def allowed(path, rules):
    keep = False
    for include, pattern in rules:
        if fnmatch.fnmatch(path, pattern):
            keep = include
    return keep


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z"], check=True, capture_output=True).stdout
    return [p for p in out.decode("utf-8").split("\0") if p]


def main(argv):
    args = list(argv)
    allowlist = "tools/publish/allowlist.txt"
    out = None
    if "--allowlist" in args:
        i = args.index("--allowlist"); allowlist = args[i + 1]; del args[i:i + 2]
    if "--out" in args:
        i = args.index("--out"); out = args[i + 1]; del args[i:i + 2]
    if out is None or args:
        print("usage: export.py --out DIR [--allowlist FILE]", file=sys.stderr)
        return 2
    if os.path.lexists(out):
        print(f"refusing: {out} already exists", file=sys.stderr)
        return 2
    if not os.path.isfile(allowlist):
        print(f"allowlist not found: {allowlist}", file=sys.stderr)
        return 2
    rules = read_allowlist(allowlist)
    copied = 0
    total = 0
    for rel in tracked_files():
        if not allowed(rel, rules):
            continue
        st = os.lstat(rel)
        if not stat.S_ISREG(st.st_mode):
            print(f"skipped (not a regular file): {rel}", file=sys.stderr)
            continue
        dest = os.path.join(out, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copy2(rel, dest)
        copied += 1
        total += st.st_size
    print(f"exported {copied} files, {total} bytes to {out}")
    result = subprocess.run([sys.executable, str(HERE / "check.py"), out])
    return result.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
