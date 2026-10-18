#!/usr/bin/env python3
"""The script natives a registration function of the game registers: name and code address.

    .venv/bin/python tools/scout/script_natives.py MODULE FUNC_RVA NAME_REGEX [--all-strings]

A registration function of the client (for the UI script's natives: client+0x3b65c0,
+0x53df70, +0x3a8b10; docs/handoff/gate3-run-9.md section 3 says how they were found) loads,
for every native in turn, its help text, its script name and its internal name and then the
address of its code.  This walks FUNC_RVA and prints, for every loaded string that matches
NAME_REGEX as a whole, the site of the string and the first code address loaded after it:

    name  string-site  code-rva

With --all-strings every string loaded between the name and the code address is printed too
(the help text names the arguments).  Prints names, strings and addresses the code refers
to; terminal use and hand-offs by module+rva.  OriginSDK.dll is refused (flow_lib).
"""

import re
import sys

from flow_lib import get
from flow_show import notes


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    every = "--all-strings" in sys.argv
    M = get(a[0])
    f = int(a[1], 16)
    rx = re.compile(a[2])
    pending = None          # (name, site)
    between = []
    for ins in M.insns(f):
        for n in notes(M, ins):
            if n.startswith("str "):
                s = eval(n[4:])
                if rx.fullmatch(s):
                    if pending:
                        print("%-40s %7x  (no code address before the next name)" % pending)
                    pending, between = (s, ins.address), []
                elif pending:
                    between.append(s)
            elif n.startswith("code ") and pending:
                t = ins.address + ins.size + [o for o in ins.operands if o.type == 3][0].mem.disp
                print("%-40s %7x  %s+0x%x" % (pending[0], pending[1], M.short, t))
                if every:
                    for s in between:
                        print("    %r" % s)
                pending = None
    if pending:
        print("%-40s %7x  (no code address after it)" % pending)


if __name__ == "__main__":
    main()
