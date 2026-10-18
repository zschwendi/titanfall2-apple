#!/usr/bin/env python3
"""Census of unwind-info versions and epilogue shapes in the game DLLs (unit rt-verify-win).

Checks the runtime's unwinder (runtime/core/unwind.c) against what the compiler emitted:

 1. every row of each DLL's exception table: unwind-info version, flags, frame register,
    and a histogram of the unwind operation codes (6 and 7 are the version 2 codes);
 2. every function is disassembled linearly, and at each instruction boundary the version 1
    epilogue rule is applied (a Python port of epilogue_v1: optional `add rsp, imm` or
    `lea rsp, [fp+disp]`, pops, then ret / rep ret / a jump).  Each ret and each jump that
    leaves the function is then counted by the shape that leads to it, and by the
    instruction just before that shape when the shape restores no stack although the
    function has unwind codes (so a form the rule does not know would show up there).

Static only: it reads the exception directory, the unwind information it points to and the
code bytes of the functions listed there.  It prints counts, a few `module+rva` examples and
instruction mnemonics; no listing.  Scope: game/bin/x64_retail/*.dll except OriginSDK.dll,
and game/server.dll.  Titanfall2.exe is never opened.

    .venv/bin/python tools/scout/unwind_census.py game
"""
import collections
import glob
import os
import struct
import sys

import capstone

SKIP = {"originsdk.dll"}
OPS = {0: "PUSH_NONVOL", 1: "ALLOC_LARGE", 2: "ALLOC_SMALL", 3: "SET_FPREG", 4: "SAVE_NONVOL", 5: "SAVE_NONVOL_FAR",
       6: "EPILOG(v2)", 7: "SPARE(v2)", 8: "SAVE_XMM128", 9: "SAVE_XMM128_FAR", 10: "PUSH_MACHFRAME"}


class Image:
    def __init__(self, path):
        self.name = os.path.basename(path)
        self.d = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.d, 0x3C)[0]
        self.machine, nsec = struct.unpack_from("<HH", self.d, pe + 4)
        opt = pe + 24
        self.ok = self.machine == 0x8664 and struct.unpack_from("<H", self.d, opt)[0] == 0x20B
        if not self.ok:
            return
        self.pdata_rva, self.pdata_size = struct.unpack_from("<II", self.d, opt + 112 + 8 * 3)
        sec = opt + struct.unpack_from("<H", self.d, pe + 20)[0]
        self.secs = [struct.unpack_from("<IIII", self.d, sec + 40 * i + 8) for i in range(nsec)]   # vsize, va, rawsize, raw

    def off(self, rva):
        for vsize, va, rawsize, raw in self.secs:
            if va <= rva < va + max(vsize, rawsize):
                return raw + (rva - va) if rva - va < rawsize else None
        return None

    def bytes(self, rva, n):
        o = self.off(rva)
        return self.d[o:o + n] if o is not None else b""


def match(code, at, begin_off, end_off, freg, chained):
    """epilogue_v1 on code[at:], offsets relative to the function's start.  Returns (lead, pops, end) or None."""
    def b(i):
        return code[i] if 0 <= i < len(code) else 0x100
    lead, p = "none", at
    if (b(p) & 0xFE) == 0x48:
        b1, b2 = b(p + 1), b(p + 2)
        if b(p) == 0x48 and b1 == 0x83 and b2 == 0xC4:
            lead, p = "add8", p + 4
        elif b(p) == 0x48 and b1 == 0x81 and b2 == 0xC4:
            lead, p = "add32", p + 7
        elif b1 == 0x8D and (b2 & 7) != 4:
            reg = ((b(p) & 1) << 3) | (b2 & 7)
            if reg != 0 and reg == freg:
                if (b2 & 0xF8) == 0x60:
                    lead, p = "lea8", p + 4
                elif (b2 & 0xF8) == 0xA0:
                    lead, p = "lea32", p + 7
    pops = 0
    while pops < 32:
        if (b(p) & 0xF8) == 0x58:
            p += 1
        elif (b(p) & 0xF0) == 0x40 and (b(p + 1) & 0xF8) == 0x58:
            p += 2
        else:
            break
        pops += 1
    pre = ""
    if b(p) == 0xF2:
        pre, p = "F2 ", p + 1
    t = b(p)
    if t == 0xC3:
        return lead, pops, pre + "ret"
    if t == 0xC2:
        return lead, pops, pre + "ret imm16"
    if t == 0xF3 and b(p + 1) == 0xC3:
        return lead, pops, pre + "rep ret"
    if t in (0xEB, 0xE9):
        if t == 0xEB:
            tgt = p + 2 + struct.unpack_from("<b", code, p + 1)[0] if p + 2 <= len(code) else None
        else:
            tgt = p + 5 + struct.unpack_from("<i", code, p + 1)[0] if p + 5 <= len(code) else None
        if tgt is None:
            return None
        if 0 <= tgt < end_off - begin_off:
            return (lead, pops, pre + "jmp to own start") if tgt == 0 and not chained else None
        return lead, pops, pre + ("jmp rel8 out" if t == 0xEB else "jmp rel32 out")     # same-function test not applied
    if t == 0xFF and b(p + 1) == 0x25:
        return lead, pops, pre + "jmp [rip+d]"
    if (t & 0xF8) == 0x48 and b(p + 1) == 0xFF and (b(p + 2) & 0x38) == 0x20:
        return lead, pops, pre + "rex jmp r/m"
    return None


def census(img, md, tot):
    n = img.pdata_size // 12
    rows = [struct.unpack_from("<III", img.bytes(img.pdata_rva + 12 * i, 12)) for i in range(n)]
    for begin, end, uw in rows:
        if uw & 1 or end <= begin:
            tot["rows"]["indirect or empty"] += 1
            continue
        h = img.bytes(uw, 4)
        if len(h) < 4:
            tot["rows"]["unwind info unreadable"] += 1
            continue
        ver, flags, count, freg = h[0] & 7, h[0] >> 3, h[2], h[3] & 15
        tot["version"][ver] += 1
        tot["flags"][flags] += 1
        tot["frame register"][freg] += 1
        codes = img.bytes(uw + 4, 2 * count)
        k, has_stack = 0, False
        while k < count and 2 * k + 1 < len(codes):
            op, oi = codes[2 * k + 1] & 15, codes[2 * k + 1] >> 4
            tot["unwind op"][OPS.get(op, "op %d" % op)] += 1
            has_stack = has_stack or op in (0, 1, 2, 3, 10)
            k += {0: 1, 2: 1, 3: 1, 10: 1, 1: 2 if oi == 0 else 3, 4: 2, 8: 2, 5: 3, 9: 3, 6: 2, 7: 3}.get(op, 1)
        if ver != 1:
            if len(tot["v2 examples"]) < 8:
                tot["v2 examples"].append("%s+0x%x" % (img.name, begin))
            continue
        code = img.bytes(begin, end - begin)
        chained = bool(flags & 4)
        prev = None
        for a, size, mn, ops in md.disasm_lite(code, 0):
            m = match(code, a, 0, len(code), freg, chained)
            is_end = mn in ("ret", "retf") or (mn == "jmp" and m is not None)
            if m is not None and not (prev and prev[0]):
                # the first boundary of a run that the rule accepts: the shape of this ending
                lead, pops, term = m
                tot["shape"][(lead, min(pops, 9), term)] += 1
                if lead == "none" and has_stack and not chained and "out" not in term and "jmp" not in term:
                    before = "%s %s" % prev[1] if prev else "(function start)"
                    if pops == 0:
                        tot["before a bare ret, function has stack codes"][before] += 1
                    else:
                        tot["before pops with no add/lea, function has stack codes"][before] += 1
                    ex = tot["examples"].setdefault(before, [])
                    if len(ex) < 2:
                        ex.append("%s+0x%x" % (img.name, begin + a))
            if mn == "ret" and m is None:
                tot["ret not accepted by the rule"][img.name] += 1
            if mn == "jmp" and m is None and ops in ("rax", "rcx", "rdx", "r8", "r9", "r10", "r11"):
                tot["jmp reg without REX.W (never an epilogue)"]["jmp " + ops] += 1
            prev = (m is not None and not is_end, (mn, ops))
            tot["instructions"]["decoded"] += 1


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "game"
    paths = sorted(glob.glob(os.path.join(root, "bin", "x64_retail", "*.dll"))) + [os.path.join(root, "server.dll")]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    tot = collections.defaultdict(collections.Counter)
    tot["v2 examples"], tot["examples"] = [], {}
    per = []
    for p in paths:
        if os.path.basename(p).lower() in SKIP or not os.path.isfile(p):
            continue
        img = Image(p)
        if not img.ok:
            continue
        before = sum(tot["version"].values())
        census(img, md, tot)
        per.append((img.name, sum(tot["version"].values()) - before))
    print("modules: %d   rows with unwind information: %d" % (len(per), sum(n for _, n in per)))
    print("  " + ", ".join("%s %d" % x for x in per))
    for key in ("rows", "version", "flags", "frame register", "unwind op", "instructions", "ret not accepted by the rule",
                "jmp reg without REX.W (never an epilogue)"):
        print("\n%s:" % key)
        for k, v in sorted(tot[key].items(), key=lambda kv: -kv[1]):
            print("  %-28s %d" % (k, v))
    print("\nversion 2 rows, first examples: %s" % (", ".join(tot["v2 examples"]) or "none"))
    print("\nendings accepted by the version 1 rule, by shape (lead, pops, terminator):")
    for (lead, pops, term), v in sorted(tot["shape"].items(), key=lambda kv: -kv[1]):
        print("  %-6s pops=%-2s %-18s %d" % (lead, "9+" if pops == 9 else pops, term, v))
    for key in ("before a bare ret, function has stack codes", "before pops with no add/lea, function has stack codes"):
        print("\n%s (top 12):" % key)
        for k, v in tot[key].most_common(12):
            print("  %-40s %-8d %s" % (k[:40], v, " ".join(tot["examples"].get(k, []))))


if __name__ == "__main__":
    main()
