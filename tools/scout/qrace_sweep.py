#!/usr/bin/env python3
"""Lock-agnostic cross-check of the queue-race survey (finding 36): the tight form of the
two-read compare-and-swap anywhere in the code, whether or not a lock call is in sight.

    .venv/bin/python tools/scout/qrace_sweep.py OUT_DIR [module.dll ...]

Looks only for the narrow form the queue template compiles to: a 64-bit `cmp reg, reg`, a
short forward `jne` over one or two instructions, the first of them `mov [base+d], reg`,
and in the eight instructions before the compare (no call or branch among them) two 8-byte
loads `mov reg, [x+d]` with that same displacement, one of them into the compared register.
Candidates are found by a byte pattern, then the function is decoded linearly to check.
It says nothing about equal bases, so every line it prints has to be read; it exists to
catch copies whose lock the region search cannot see (a spin lock, a lock taken by the
caller). Writes OUT_DIR/_sweep.json and prints one line per site.
"""
import json
import multiprocessing as mp
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qrace_lib as Q  # noqa: E402

PAT = re.compile(rb"[\x48\x49\x4c\x4d][\x39\x3b][\xc0-\xff]\x75[\x02-\x0c]", re.S)


def work(name):
    ctx = Q.Ctx(name)
    m = ctx.m
    out, cache = [], {}
    for s in m.exec_sections():
        buf = m.data[s["rptr"]:s["rptr"] + s["rsize"]]
        for mt in PAT.finditer(buf):
            rva = s["va"] + mt.start()
            ent = m.entry_of(rva)
            if ent is None:
                continue
            if ent[0] not in cache:
                if len(cache) > 64:
                    cache.clear()
                ins = ctx.dis.linear(ent[0], ent[1])
                cache[ent[0]] = (ins, {i.rva: n for n, i in enumerate(ins)})
            ins, at = cache[ent[0]]
            n = at.get(rva)
            if n is None or n + 2 >= len(ins):
                continue
            cmp_, jcc, st = ins[n], ins[n + 1], ins[n + 2]
            if cmp_.mn != "cmp" or jcc.mn != "jne" or st.mn != "mov" or len(st.ops) != 2:
                continue
            d = st.ops[0]
            if d[0] != "m" or d[7] != 8 or d[1] in (None, "sp") or d[6] == "rip" or st.ops[1][0] != "r":
                continue
            if not (st.rva < jcc.target <= st.rva + 12):
                continue
            regs = {cmp_.ops[0][1], cmp_.ops[1][1]}
            loads, fed = [], False
            for k in range(n - 1, max(-1, n - 9), -1):
                i = ins[k]
                if i.is_call or i.is_jmp or i.is_jcc or i.is_ret:
                    break
                if i.mn == "mov" and len(i.ops) == 2 and i.ops[0][0] == "r" and i.ops[1][0] == "m":
                    o = i.ops[1]
                    if o[7] == 8 and o[4] == d[4] and o[1] not in (None, "sp") and o[6] != "rip" and o[2] is None:
                        loads.append(i.rva)
                        if i.ops[0][1] in regs and not fed:
                            fed = True
                if i.ops and i.ops[0][0] == "m" and i.mn not in ("cmp", "test") and i.ops[0][1] != "sp":
                    break  # a store in between
            if len(loads) >= 2 and fed:
                out.append({"fn": "0x%x" % ctx.func_of(rva), "reads": ["0x%x" % a for a in sorted(loads)],
                            "store": "0x%x" % st.rva, "lock_call_in_fn": ctx.func_of(rva) in ctx.lock_fns})
    return name, out


def main():
    out = sys.argv[1]
    names = sys.argv[2:] or Q.survey_modules()
    res = {}
    with mp.Pool(min(4, len(names))) as pool:
        for name, hits in pool.imap_unordered(work, names):
            res[name] = hits
            for h in hits:
                print("%-24s fn %-9s reads %-22s store %-9s lock call in function: %s" % (
                    name, h["fn"], ",".join(h["reads"]), h["store"], "yes" if h["lock_call_in_fn"] else "NO"), flush=True)
            print("%-24s %d site(s)" % (name, len(hits)), flush=True)
    with open(os.path.join(out, "_sweep.json"), "w") as fh:
        json.dump(res, fh, indent=1)


if __name__ == "__main__":
    main()
