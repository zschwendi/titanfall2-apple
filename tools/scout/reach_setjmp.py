#!/usr/bin/env python3
"""setjmp and longjmp in the boot-path modules: the copies, the imports and every caller
(finding 38, question 2).

    .venv/bin/python tools/scout/reach_setjmp.py [MODULE ...] [--callers] [--up N]

Without module names it scans the 20 boot-path modules.  For each module:

  imports   any import whose name contains "setjmp" or "longjmp", and RtlUnwindEx (the
            function a frame-aware longjmp goes through)
  copies    private copies of the C runtime's routines, found by what they must do, not by
            name: a save routine stores MXCSR at [rcx+0x58] and the x87 control word at
            [rcx+0x5c] (the two instructions are adjacent in every copy seen); a restore
            routine loads both from the same offsets.  The enclosing function is the copy.
            For a save routine the script also shows whether it starts by loading a pointer
            from a global and jumping through it when non-zero (the hook that a second,
            frame-recording variant installs), and for a restore routine whether it reaches
            RtlUnwindEx.
  callers   with --callers: every direct call or jump to each copy, the function it sits
            in, that function's RTTI/export label and a few of its strings; for a save
            call, what the caller passes as the second argument (rdx): zero means the
            restore will not unwind frames, anything else means it will go through
            RtlUnwindEx.
  --up N    also walks N levels up from each caller (reach_up).

Offsets 0x58 and 0x5c are those of the x64 jump buffer in Microsoft's <setjmp.h>
(MxCsr, FpCsr); they are used here as a signature only.  Prints addresses, names from the
image, single instructions and counts.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reach_lib as R  # noqa: E402
import xref_lib as X  # noqa: E402

SAVE = re.compile(rb"\x0f\xae\x59\x58.{0,8}?\xd9\x79\x5c|\xd9\x79\x5c.{0,8}?\x0f\xae\x59\x58", re.S)
LOAD = re.compile(rb"\x0f\xae\x51\x58.{0,8}?\xd9\x69\x5c|\xd9\x69\x5c.{0,8}?\x0f\xae\x51\x58", re.S)


def find_copies(m, pat):
    out = []
    for s in m.exec_sections():
        buf = m.data[s["rptr"]:s["rptr"] + s["rsize"]]
        for mt in pat.finditer(buf):
            site = s["va"] + mt.start()
            f = m.unit_of(site)
            if f is not None and f not in [x[0] for x in out]:
                out.append((f, site))
    return out


def head(m, f, n=4):
    out = []
    pc = f
    for _ in range(n):
        ins = m.dis.at(pc)
        if ins is None:
            break
        out.append(ins)
        pc += ins.size
    return out


def rdx_before(m, site, back=12):
    """What the caller puts in rdx before the call at `site`: the last write to rdx/edx in
    the straight run of instructions before it (text of that one instruction), or '?'."""
    f = m.unit_of(site)
    e = m.entry_of(site)
    pc = e[0] if e is not None else f
    run = []
    while pc is not None and pc < site:
        ins = m.dis.at(pc)
        if ins is None:
            pc += 1
            continue
        run.append(ins)
        pc += ins.size
    for ins in reversed(run[-back:]):
        if ins.is_call or ins.is_jmp or ins.is_ret:
            break
        if "d" in ins.wr:
            return "%s %s" % (ins.mn, ins.text)
    return "?"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    up_n = 0
    if "--up" in sys.argv:
        up_n = int(sys.argv[sys.argv.index("--up") + 1])
        args = [a for a in args if a != str(up_n)]
    mods = args or R.BOOT
    want_callers = "--callers" in sys.argv or up_n
    total_save = total_load = 0
    for name in mods:
        m = R.Mod(name)
        slots = X.Module(m.path).iat_slots()
        imps = sorted("%s!%s" % (d, n) for _s, (d, n, _k) in slots.items()
                      if "setjmp" in n.lower() or "longjmp" in n.lower() or n == "RtlUnwindEx")
        saves = find_copies(m, SAVE)
        loads = find_copies(m, LOAD)
        total_save += len(saves)
        total_load += len(loads)
        print("%-20s imports: %-28s save copies: %-22s restore copies: %s" % (
            name, ", ".join(i.split("!")[1] for i in imps) or "-",
            ", ".join("+0x%x" % f for f, _s in saves) or "-",
            ", ".join("+0x%x" % f for f, _s in loads) or "-"))
        if not want_callers:
            continue
        unwind_slots = {s for s, (d, n, _k) in slots.items() if n == "RtlUnwindEx"}
        for kind, lst in (("save", saves), ("restore", loads)):
            for f, _site in lst:
                h = head(m, f)
                note = ""
                if kind == "save" and h and h[0].mn == "mov" and any(op[0] == "m" and op[6] == "rip" for op in h[0].ops):
                    hook = h[0].rva + h[0].size + [op for op in h[0].ops if op[0] == "m"][0][4]
                    writers = [a for a, mn, _t, fm in m.rip_refs(hook, hook + 8) if fm and mn == "mov"]
                    note = "; starts by loading a hook from +0x%x (%s, %d instruction(s) in the image store to it)" % (
                        hook, m.sec_name(hook), len(writers))
                if kind == "restore":
                    end = f + 0x200
                    ru = [a for a, _sz, mn, t, _fm in m.x()["rip"] if f <= a < end and t in unwind_slots]
                    note = "; calls RtlUnwindEx at %s" % ", ".join("+0x%x" % a for a in ru) if ru else "; no call of RtlUnwindEx within 0x200 bytes"
                refs = [r for r in m.refs([f])[f]]
                print("   %s copy %s+0x%x%s" % (kind, name, f, note))
                if not refs:
                    print("      no static reference")
                for site, k, u in refs:
                    if k == "data":
                        print("      pointer in data at +0x%x (%s)" % (site, u))
                        continue
                    extra = ""
                    if kind == "save" and k in ("call", "jmp"):
                        extra = "; rdx <- %s" % rdx_before(m, site)
                    print("      %s at +0x%x in %s%s" % (k, site, m.label(u), extra))
                    if up_n and isinstance(u, int):
                        import reach_up
                        reach_up.up(name, u, up_n, True, 0, out=lambda s: print("        " + s[:200]))
    print("copies in %d modules: %d save, %d restore" % (len(mods), total_save, total_load))


if __name__ == "__main__":
    main()
