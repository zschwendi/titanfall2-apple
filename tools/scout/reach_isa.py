#!/usr/bin/env python3
"""The functions of engine.dll that the lifter skipped for an instruction-set extension:
their code units, every reference into a unit from code that *was* lifted, and what the
referring function reads of the capability words before it gets there (finding 38, Q1).

    .venv/bin/python tools/scout/reach_isa.py CENSUS_DIR [--units] [--edges] [--guards] [--capreaders]

A *code unit* is one stretch of skipped code: the unwind range(s) of a skipped function plus
the short entry sequence that some of them have in front of their unwind range (13 bytes,
three moves that save rdi, rsi and rsp, or 8 bytes; the unwind range starts after it), plus skipped
functions the lifter cut out of the same stretch ("tail-into" entries).  Units are what a
caller can reach; the lifter's 83 functions fall into fewer units.

  --units       one line per unit: extent, the lifter functions in it, the instruction
                families refused, and how many references enter it from outside the units
  --edges       every reference into a unit from outside all units: site, kind (call, jmp,
                jcc, lea, data), the referring function and whether that function is lifted
  --guards      for each referring function: its reads of the capability words, the test
                applied (mask or bit, when the next few instructions show one) and the
                conditional jump that follows.  One instruction per line; the analyst reads
                the condition off these lines.
  --capreaders  every function that reads the capability words, lifted or not, with the
                masks it applies: the complete list of decision points.

  --slice LO:HI the decision instructions in [LO, HI): loads of a capability word, tests
                (test, bt, and, or, cmp, shr), conditional moves, jumps and calls.  For
                reading one guard; a call or jump into a unit is marked.

CAP is the 16-byte vector the detection routine fills; its address is read from the code
(the argument of the calls to the skipped detection routine) and checked, not assumed.
Prints addresses, single instructions and counts.
"""

import bisect
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reach_lib as R  # noqa: E402
import reach_skips as K  # noqa: E402

MODULE = "engine"
DETECT_REASON = "xgetbv"


def find_cap(m, detect):
    """The address passed in rcx at the calls to the detection routine (`lea rcx, [rip+..]`
    right before each call).  All call sites must agree."""
    caps = set()
    for site, kind, _u in m.refs([detect])[detect]:
        if kind != "call":
            continue
        back = [r for r in m.rip_refs(0, 1 << 40) if 0 < site - r[0] <= 7 and r[1] == "lea"]
        for a, _mn, t, _fm in back:
            ins = m.dis.at(a)
            if ins and ins.ops and ins.ops[0][0] == "r" and ins.ops[0][1] == "c" and a + ins.size == site:
                caps.add(t)
    if len(caps) != 1:
        raise SystemExit("capability vector not determined: %s" % sorted(hex(c) for c in caps))
    return caps.pop()


def units(m, rep):
    """[(lo, hi, [lifter function rvas], Counter of refused mnemonic)] sorted."""
    sk = [r for r in R.skipped(rep) if K.classify(r[3]) != "ijmp"]
    near = sorted(min(a for a, _b in r[3]["ranges"]) for r in sk)
    report_fns = [f for f in rep["functions"] if any(0 < x - f["ranges"][0][0] <= 16 for x in near[bisect.bisect_right(near, f["ranges"][0][0]):][:1])]
    spans = []
    for rva, _kind, _what, rec in sk:
        lo = min(a for a, _b in rec["ranges"])
        hi = max(b for _a, b in rec["ranges"])
        # the 13-byte entry sequence in front of an unwind range that starts at ...0d
        if rec["origin"] == "pdata" and (lo & 0xF) == 0xD:
            a, ok = lo - 0xD, True
            for want in ("mov", "mov", "mov"):
                ins = m.dis.at(a)
                if ins is None or ins.mn != want:
                    ok = False
                    break
                a += ins.size
            if ok and a == lo:
                lo -= 0xD
        # a short translated function that ends exactly where this one starts is its entry
        # sequence (the lifter found the entry through a reference and cut it off there)
        for f in report_fns:
            if f.get("status") != "skipped" and len(f["ranges"]) == 1 and f["ranges"][0][1] == lo \
                    and 0 < lo - f["ranges"][0][0] <= 16:
                lo = f["ranges"][0][0]
        spans.append([lo, hi, [rva], collections.Counter(K.mnem(r.get("what", "")) for r in rec["reasons"])])
    spans.sort()
    out = []
    for s in spans:
        if out and s[0] < out[-1][1]:
            out[-1][1] = max(out[-1][1], s[1])
            out[-1][2] += s[2]
            out[-1][3].update(s[3])
        else:
            out.append(s)
    return out


def family(mn):
    fams = set()
    for x in mn:
        if x in K.SHA:
            fams.add("SHA")
        elif x in K.AES or x.startswith("vaes"):
            fams.add("AES")
        elif "clmul" in x:
            fams.add("CLMUL")
        elif x.startswith("v"):
            fams.add("AVX")
        elif x in K.BMI:
            fams.add("BMI")
        else:
            fams.add(x)
    return "+".join(sorted(fams))


def edges_into(m, us):
    """{unit index: [(site, kind, referrer, target)]} from outside every unit."""
    def unit_index(a):
        for i, (lo, hi, _f, _c) in enumerate(us):
            if lo <= a < hi:
                return i
        return None

    idx = m._index()
    out = collections.defaultdict(list)
    noise = collections.Counter()
    for t, lst in idx.items():
        i = unit_index(t)
        if i is None:
            continue
        for site, kind in lst:
            if unit_index(site) is not None:
                continue
            owner = m.lifted_owner(site)
            if owner is None:
                noise[i] += 1          # not an instruction of any translated function
                continue
            out[i].append((site, kind, owner, t))
    lo_all, hi_all = us[0][0], us[-1][1]
    for t, sites in m.abs_refs(range(lo_all, hi_all)).items():
        i = unit_index(t)
        if i is not None:
            for s in sites:
                out[i].append((s, "data", m.sec_name(s), t))
    for i in out:
        out[i].sort()
    return out, noise


def cap_reads(m, cap, f_lo, f_hi):
    """Reads of the capability words in [f_lo, f_hi): (site, text of the instruction, word
    index, then up to 6 following instructions that are a test of some kind or a jcc)."""
    out = []
    for a, mn, t, _fm in m.rip_refs(cap, cap + 16):
        if not (f_lo <= a < f_hi):
            continue
        ins = m.dis.at(a)
        follow = []
        pc = a + ins.size
        for _ in range(8):
            nx = m.dis.at(pc)
            if nx is None:
                break
            if nx.mn in ("test", "bt", "and", "cmp", "shr", "or", "andn") or nx.is_jcc or nx.mn.startswith("cmov"):
                follow.append("%x %s %s" % (nx.rva, nx.mn, nx.text))
            if nx.is_jcc or nx.is_ret or nx.is_jmp or nx.is_call:
                break
            pc += nx.size
        out.append((a, "%s %s" % (ins.mn, ins.text), (t - cap) // 4, follow))
    return out


def extent(m, rep_by_rva, f):
    rec = rep_by_rva.get(f)
    if rec:
        return min(a for a, _b in rec["ranges"]), max(b for _a, b in rec["ranges"])
    e = m.func_end(f)
    return f, e if e else f + 0x400


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cdir = R.census_dir(args[0] if args else None)
    rep = R.load_report(MODULE, cdir)
    m = R.Mod(MODULE, rep)
    m.set_cfg(R.load_cfg(MODULE, cdir))
    by_rva = {int(f["rva"], 16): f for f in rep["functions"]}
    status = {r: f["status"] for r, f in by_rva.items()}
    detect = [r[0] for r in R.skipped(rep) if r[2].startswith(DETECT_REASON)]
    if len(detect) != 1:
        raise SystemExit("expected one function skipped for %s, found %d" % (DETECT_REASON, len(detect)))
    detect = detect[0]
    cap = find_cap(m, detect)
    us = units(m, rep)
    ed, noise = edges_into(m, us)
    nfun = sum(len(u[2]) for u in us)
    print("%s: %d skipped functions (not counting indirect jumps) in %d code units; detection routine +0x%x; "
          "capability words at +0x%x..+0x%x" % (MODULE, nfun, len(us), detect, cap, cap + 15))
    print("references into a unit from places where the lifter decoded no instruction (data read as code by the "
          "linear decode, or code the lifter never found): %d, left out below" % sum(noise.values()))
    if "--units" in sys.argv:
        for i, (lo, hi, fs, c) in enumerate(us):
            kinds = collections.Counter(k for _s, k, _u, _t in ed.get(i, ()))
            print("  unit %2d  +0x%x..+0x%x  %-14s lifter functions: %-34s refs from outside: %s"
                  % (i, lo, hi, family(c), ", ".join("0x%x" % f for f in fs),
                     ", ".join("%s %d" % kv for kv in sorted(kinds.items())) or "none"))
    referrers = collections.defaultdict(list)
    for i in sorted(ed):
        for site, kind, u, t in ed[i]:
            if isinstance(u, int):
                referrers[u].append((site, kind, i, t))
    if "--edges" in sys.argv:
        for i in sorted(ed):
            for site, kind, u, t in ed[i]:
                if isinstance(u, int):
                    print("  unit %2d  %-4s at +0x%x -> +0x%x  in +0x%x (%s)" % (i, kind, site, t, u, status.get(u, "not a lifter function")))
                else:
                    print("  unit %2d  data pointer at +0x%x (%s) -> +0x%x" % (i, site, u, t))
    if "--guards" in sys.argv:
        for f in sorted(referrers):
            lo, hi = extent(m, by_rva, f)
            print("  +0x%x (%s), enters %s" % (f, status.get(f, "?"), ", ".join(
                "unit %d by %s at +0x%x" % (i, k, s) for s, k, i, _t in referrers[f][:6])
                + (" ... %d more" % (len(referrers[f]) - 6) if len(referrers[f]) > 6 else "")))
            reads = cap_reads(m, cap, lo, hi)
            if not reads:
                print("      reads no capability word")
            for a, text, word, follow in reads:
                print("      +0x%x  word %d  %s   ; %s" % (a, word, text, " ; ".join(follow)))
    for a in sys.argv[1:]:
        if a.startswith("--slice="):
            lo, hi = (int(x, 16) for x in a.split("=", 1)[1].split(":"))
            pc = lo
            while pc < hi:
                ins = m.dis.at(pc)
                if ins is None:
                    pc += 1
                    continue
                tgt = ins.target
                rip = None
                for op in ins.ops:
                    if op[0] == "m" and op[6] == "rip":
                        rip = ins.rva + ins.size + op[4]
                note = ""
                if rip is not None and cap <= rip < cap + 16:
                    note = "   <- capability word %d" % ((rip - cap) // 4)
                if tgt is not None:
                    for i, (ulo, uhi, _f, _c) in enumerate(us):
                        if ulo <= tgt < uhi:
                            note = "   -> unit %d (skipped code)" % i
                    if not note and status.get(tgt) is not None and ins.is_call:
                        note = "   (%s)" % status[tgt]
                if note or ins.mn in ("test", "bt", "and", "or", "cmp", "shr", "ret") or ins.mn.startswith(("j", "cmov", "call")):
                    print("    +0x%x  %-7s %s%s" % (pc, ins.mn, ins.text, note))
                pc += ins.size
    if "--capreaders" in sys.argv:
        byf = collections.defaultdict(list)
        for a, mn, t, fm in m.rip_refs(cap, cap + 16):
            byf[m.unit_of(a)].append((a, mn, t, fm))
        print("  %d functions read or write the capability words" % len(byf))
        for f in sorted(byf):
            words = collections.Counter(((t - cap) // 4, "w" if (fm and mn in ("mov", "and", "or")) else "r") for _a, mn, t, fm in byf[f])
            print("  +0x%x (%s): %s" % (f, status.get(f, "not a lifter function"),
                                       ", ".join("word %d %s x%d" % (w, rw, n) for (w, rw), n in sorted(words.items()))))


if __name__ == "__main__":
    main()
