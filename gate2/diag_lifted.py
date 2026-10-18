#!/usr/bin/env python3
"""A DIAGNOSTIC copy of one lifted C file in which the C of ONE guest instruction is replaced.

    make gate2-diag DIAG=<name> DIAG_RVA=<hex rva> DIAG_C='<C statements>' [DIAG_MODULE=tier0]

(gate2/gate2.mk runs this, compiles the copy and links build/gate2*-diag-<name>/gate2.)  For an
experiment on a fault of guest code: "what if this load returned the value that load returned",
"stop here when these two registers differ", "wait here".  The lifter writes every instruction as

    /* 1d627: mov rcx, qword ptr [rax] */
    { ...the C... }

and this script replaces the one line after the comment of the instruction at --rva with
`{ <C statements> }`, in a copy under --out, and prints the copy's path.  In the statements,
`@@` stands for the original line, so that an experiment can add to an instruction instead of
replacing it.  The lifted C under lifted/ is not touched; the copy is build output, derived from
a game file like the rest of it, and is never committed.
"""

import argparse
import glob
import os
import re
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--module", required=True)
    ap.add_argument("--lifted", required=True, help="directory with the lifter's output for the module")
    ap.add_argument("--rva", required=True, help="hex rva of the instruction")
    ap.add_argument("--c", required=True, help="the C statements that replace the instruction's line (@@: the original)")
    ap.add_argument("--out", required=True, help="directory for the copy")
    a = ap.parse_args()

    rva = "%x" % int(a.rva, 16)
    head = re.compile(r"^\s*/\* %s: .* \*/\s*$" % rva)
    hits = []
    for path in sorted(glob.glob(os.path.join(a.lifted, a.module + "_[0-9]*.c"))):
        with open(path) as f:
            lines = f.readlines()
        for i, ln in enumerate(lines):
            if head.match(ln):
                hits.append((path, lines, i))
    if len(hits) != 1:
        sys.exit("diag_lifted: %d instructions at rva 0x%s in %s (want exactly 1)" % (len(hits), rva, a.lifted))
    path, lines, i = hits[0]
    body = lines[i + 1]
    if not (body.lstrip().startswith("{") and body.rstrip().endswith("}")):
        sys.exit("diag_lifted: the line after the comment of 0x%s is not a one-line block" % rva)
    lines[i + 1] = "    { /* DIAGNOSTIC (gate2/diag_lifted.py) */ %s }\n" % a.c.replace("@@", body.strip())
    os.makedirs(a.out, exist_ok=True)
    out = os.path.join(a.out, os.path.basename(path))
    with open(out, "w") as f:
        f.writelines(lines)
    print(out)


if __name__ == "__main__":
    main()
