#!/usr/bin/env python3
"""Which functions are copies of tier0's queue routines, by structure (finding 36).

    .venv/bin/python tools/scout/qrace_template.py [--min 0.75] [module.dll ...]

Reference routines, all in tier0.dll: push +0x1d5e0, the out-of-line swap of one link
+0x1c810, the swap of a {node, sequence} pair +0x1c740, pop +0x1d080. Every function of
every surveyed DLL that makes a lock call is reduced to a sequence of tokens (mnemonic,
operand kinds and sizes, displacements of non-stack memory operands; no register names) and
compared with each reference by difflib's ratio. Prints, per module: the functions at or
above --min, and the functions that load the address of the pop routine's message string
("Bad node link detected", one per copy of pop). For each push copy it also prints whether
the function writes its first argument through its second before the first lock call (the
unlocked write of the end mark into the new node's link) and how many locked regions it has.

Output is addresses, ratios and counts only.
"""
import difflib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qrace_lib as Q  # noqa: E402

REFS = [("push", 0x1d5e0), ("swap1", 0x1c810), ("swap2", 0x1c740), ("pop", 0x1d080)]
MESSAGE = "Bad node link detected"


def tokens(ctx, f):
    out = []
    for b, e in ctx.groups.get(f, []):
        for i in ctx.dis.linear(b, e):
            t = [i.mn]
            for o in i.ops:
                if o[0] == "m":
                    t.append("m%d:%s" % (o[7], "sp" if o[1] == "sp" else "ip" if o[6] == "rip" else "%x" % (o[4] & 0xffff)))
                elif o[0] == "r":
                    t.append("r%d" % o[3])
                else:
                    t.append(o[0])
            out.append(" ".join(t))
    return out


def endmark_store(ctx, f):
    """rva of `mov [arg2], arg1` before the first call of f, or None."""
    alias = {"c": "this", "d": "node"}
    b, e = ctx.groups[f][0]
    for i in ctx.dis.linear(b, e):
        if i.is_call or i.is_ret or i.is_jmp:
            return None
        if i.mn == "mov" and len(i.ops) == 2:
            d, s = i.ops
            if d[0] == "m" and s[0] == "r" and alias.get(d[1]) == "node" and d[4] == 0 and d[2] is None \
                    and alias.get(s[1]) == "this":
                return i.rva
            if d[0] == "r":
                if s[0] == "r" and s[1] in alias:
                    alias[d[1]] = alias[s[1]]
                else:
                    alias.pop(d[1], None)
        else:
            for r in i.wr:
                alias.pop(r, None)
    return None


def main():
    lo = float(sys.argv[sys.argv.index("--min") + 1]) if "--min" in sys.argv else 0.75
    names = [a for a in sys.argv[1:] if a.endswith(".dll")] or Q.survey_modules()
    t0 = Q.Ctx("tier0.dll")
    ref = [(n, tokens(t0, f)) for n, f in REFS]
    for name in names:
        ctx = t0 if name == "tier0.dll" else Q.Ctx(name)
        m = ctx.m
        msg = [r for r, s in m.strings().items() if s.startswith(MESSAGE)]
        users = sorted({ctx.func_of(site) for site, tgt in m.lea_refs() if tgt in msg} - {None})
        rows = []
        for f in ctx.lock_functions():
            tk = tokens(ctx, f)
            for n, rt in ref:
                if not (0.5 * len(rt) <= len(tk) <= 2 * len(rt)):
                    continue
                sm = difflib.SequenceMatcher(None, rt, tk, autojunk=False)
                if sm.quick_ratio() < lo:
                    continue
                r = sm.ratio()
                if r >= lo:
                    rows.append((n, f, r))
        print("%s: message string loaded by %d function(s): %s" % (
            name, len(users), " ".join("0x%x" % u for u in users)))
        for n, f, r in sorted(rows):
            extra = ""
            if n == "push":
                w = endmark_store(ctx, f)
                extra = "  end-mark write before the lock: %s; locked regions %d" % (
                    "0x%x" % w if w is not None else "not seen", len(ctx.enter_sites(f)))
            print("   %-6s 0x%-8x ratio %.2f%s" % (n, f, r, extra))


if __name__ == "__main__":
    main()
