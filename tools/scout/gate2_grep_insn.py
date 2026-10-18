#!/usr/bin/env python3
"""Find instructions of tier0.dll or vstdlib.dll by a regular expression on "mnemonic operands".

    .venv/bin/python tools/scout/gate2_grep_insn.py vstdlib 'mov dword ptr \\[r.. \\+ 0x18\\], [14]$'
    .venv/bin/python tools/scout/gate2_grep_insn.py tier0 '^lock ' --count      # per mnemonic only
    .venv/bin/python tools/scout/gate2_grep_insn.py vstdlib '^xchg' --fn 0x17350  # one function only
    .venv/bin/python tools/scout/gate2_grep_insn.py tier0 . --target 0x19a530:0x19a570   # rip-relative operands
                                                                                         # that land in [lo, hi)

Walks the ranges of every function in the lifter's report (lifted/<module>/<module>.report.json) and
prints, for each match, the instruction's rva, the function it belongs to and the instruction.  With
--count it prints only how many matches each mnemonic has.  Output is for the analyst's screen;
nothing is stored.
"""

import collections
import re
import sys

import capstone

import gate2_lib as L


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    M = L.load(sys.argv[1])
    pat = re.compile(sys.argv[2])
    count = "--count" in sys.argv
    only = int(sys.argv[sys.argv.index("--fn") + 1], 16) if "--fn" in sys.argv else None
    target = None
    if "--target" in sys.argv:
        lo_s, hi_s = sys.argv[sys.argv.index("--target") + 1].split(":")
        target = (int(lo_s, 16), int(hi_s, 16))
    rip = re.compile(r"\[rip ([+-]) (0x[0-9a-f]+)\]")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    seen = collections.Counter()
    for fn in M.rbegins:
        if only is not None and fn != only:
            continue
        for lo, hi in M.rfun[fn]["ranges"]:
            off = M.pe.rva_to_off(lo)
            if off is None:
                continue
            for ins in md.disasm(M.pe.data[off:off + (hi - lo)], lo):
                text = f"{ins.mnemonic} {ins.op_str}".strip()
                if not pat.search(text):
                    continue
                where = ""
                if target is not None:
                    m = rip.search(text)
                    if not m:
                        continue
                    t = ins.address + ins.size + (int(m.group(2), 16) if m.group(1) == "+" else -int(m.group(2), 16))
                    if not target[0] <= t < target[1]:
                        continue
                    where = f"   -> +0x{t:x}"
                seen[ins.mnemonic] += 1
                if not count:
                    print(f"  +0x{ins.address:x}  fn +0x{fn:x}  {text}{where}")
    if count:
        for m, n in seen.most_common():
            print(f"  {n:6d}  {m}")
    print(f"{sum(seen.values())} match(es) in {sys.argv[1]}")


if __name__ == "__main__":
    main()
