#!/usr/bin/env python3
"""Does plain C on arm64 (FPCR default / FPCR.AH) give x86 SSE results bit for bit?  (issue #11)

For every SSE operation: generate inputs -> run the real x86 instruction in Unicorn (the oracle)
-> run each candidate C formulation (afp_lab.c) on arm64 under a chosen FPCR -> compare bits.

  afp_lab.py build
  afp_lab.py run [--ops add*,cvt*] [--jobs 8] [--quick] [--out out/results.json]
  afp_lab.py summary [--file out/results.json] [--ops ...]

Scripts that use multiprocessing must be run from a file, never from stdin; this one is.
"""
import argparse
import fnmatch
import json
import os
import struct
import subprocess
import sys
import time
from multiprocessing import Pool

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
OUT = os.path.join(HERE, "out")
EXE = os.path.join(OUT, "afp_lab")

from afp_x86 import X86, CMP_PRED, all_op_names                 # noqa: E402
from afp_inputs import Gen, DOM_W                                # noqa: E402
import afp_model                                                 # noqa: E402

# ---- FPCR bits (Arm ARM): FIZ=0 AH=1 NEP=2, RMode=23:22, FZ=24
FIZ, AH, NEP, FZ = 1, 2, 4, 1 << 24
RC_TO_RMODE = {0: 0, 1: 2, 2: 1, 3: 3}      # x86 RC: 0 nearest 1 down 2 up 3 zero  ->  Arm RN=0 RP=1 RM=2 RZ=3


def fpcr_for(mode, rc, daz, ftz):
    v = RC_TO_RMODE[rc] << 22
    if mode == "def":                         # no AFP bits: FZ flushes inputs and outputs together
        if daz or ftz:
            v |= FZ
    elif mode in ("ah", "ah_nep"):
        v |= AH
        if daz:
            v |= FIZ
        if ftz:
            v |= FZ
        if mode == "ah_nep":
            v |= NEP
    elif mode == "fiz":                       # FIZ/FZ without AH
        if daz:
            v |= FIZ
        if ftz:
            v |= FZ
    else:
        raise ValueError(mode)
    return v


def mxcsr_for(rc, daz, ftz):
    return 0x1f80 | (rc << 13) | (daz << 6) | (ftz << 15)


# ---------------------------------------------------------------------------------------------
# operation table
# ---------------------------------------------------------------------------------------------
def build_ops():
    ops = {}

    def add(name, dom, nl, src, rc=False, daz=False, ftz=False, ftzm=None, cname=None, dom_a=None, note=""):
        ops[name] = dict(name=name, dom=dom, dom_a=dom_a or dom, nl=nl, src=src, rc=rc, daz=daz, ftz=ftz,
                         ftzm=ftzm, cname=cname or name, note=note)

    for sfx, dom, nl in (("ss", "f32", 1), ("sd", "f64", 1), ("ps", "f32", 4), ("pd", "f64", 2)):
        w = DOM_W[dom]
        for fam in ("add", "sub", "mul", "div"):
            add(fam + sfx, dom, nl, "ab", rc=True, daz=True, ftz=True, ftzm=("bin", fam, w, w, nl))
        add("sqrt" + sfx, dom, nl, "b", rc=True, daz=True)
        for fam in ("min", "max"):
            add(fam + sfx, dom, nl, "ab", daz=True)
        for i, p in enumerate(CMP_PRED):
            add(f"cmp{p}{sfx}", dom, nl, "ab", daz=True, cname=f"cmp{i}{sfx}")
    add("rcpss", "f32", 1, "b", daz=True, ftz=True, ftzm=("rcp", None, 32, 32, 1))
    add("rcpps", "f32", 4, "b", daz=True, ftz=True, ftzm=("rcp", None, 32, 32, 4))
    add("rsqrtss", "f32", 1, "b", daz=True)
    add("rsqrtps", "f32", 4, "b", daz=True)
    add("comiss", "f32", 1, "ab", daz=True)
    add("ucomiss", "f32", 1, "ab", daz=True)
    add("comisd", "f64", 1, "ab", daz=True)
    add("ucomisd", "f64", 1, "ab", daz=True)
    add("cvtsi2ss32", "i32", 1, "a", rc=True)
    add("cvtsi2ss64", "i64", 1, "a", rc=True)
    add("cvtsi2sd32", "i32", 1, "a")
    add("cvtsi2sd64", "i64", 1, "a", rc=True)
    add("cvttss2si32", "f32", 1, "b", daz=True)
    add("cvttss2si64", "f32", 1, "b", daz=True)
    add("cvttsd2si32", "f64", 1, "b", daz=True)
    add("cvttsd2si64", "f64", 1, "b", daz=True)
    add("cvtss2si32", "f32", 1, "b", rc=True, daz=True)
    add("cvtss2si64", "f32", 1, "b", rc=True, daz=True)
    add("cvtsd2si32", "f64", 1, "b", rc=True, daz=True)
    add("cvtsd2si64", "f64", 1, "b", rc=True, daz=True)
    add("cvtss2sd", "f32", 1, "b", daz=True)
    add("cvtsd2ss", "f64", 1, "b", rc=True, daz=True, ftz=True, ftzm=("cvt", None, 64, 32, 1))
    add("cvtps2pd", "f32", 2, "b", daz=True)
    add("cvtpd2ps", "f64", 2, "b", rc=True, daz=True, ftz=True, ftzm=("cvt", None, 64, 32, 2))
    add("cvtdq2ps", "i32", 4, "b", rc=True)
    add("cvtdq2pd", "i32", 2, "b")
    add("cvtps2dq", "f32", 4, "b", rc=True, daz=True)
    add("cvttps2dq", "f32", 4, "b", daz=True)
    add("cvtpd2dq", "f64", 2, "b", rc=True, daz=True)
    add("cvttpd2dq", "f64", 2, "b", daz=True)
    assert set(ops) == set(all_op_names()), set(ops) ^ set(all_op_names())
    return ops


OPS = build_ops()


def configs_for(op):
    s = OPS[op]
    out = []
    for rc in (0, 1, 2, 3) if s["rc"] else (0,):
        for daz in (0, 1) if s["daz"] else (0,):
            for ftz in (0, 1) if s["ftz"] else (0,):
                out.append((rc, daz, ftz))
    return out


def modes_for(cfg):
    rc, daz, ftz = cfg
    m = ["def", "ah"]
    if (rc, daz, ftz) == (0, 0, 0):
        m.append("ah_nep")
    if daz or ftz:
        m.append("fiz")
    return m


# ---------------------------------------------------------------------------------------------
# input records
# ---------------------------------------------------------------------------------------------
class Settings:
    n_uniform = 100_000
    n_struct = 60_000


_pair_cache = {}
_rec_cache = {}


def pairs_for(dom):
    """-> (pairs, segments) where segments = [(name, start, end)] index ranges into pairs."""
    if dom not in _pair_cache:
        g = Gen(dom, seed={"f32": 11, "f64": 22, "i32": 33, "i64": 44}[dom])
        segs, pairs = [], []
        for name, lst in (("uniform", g.uniform_pairs(Settings.n_uniform)),
                          ("structured", g.structured_pairs(Settings.n_struct)),
                          ("boundary", g.boundary_pairs())):
            segs.append((name, len(pairs), len(pairs) + len(lst)))
            pairs += lst
        _pair_cache[dom] = (pairs, segs, g.B)
    return _pair_cache[dom]


def records_for(op):
    """-> (records bytes, segments by record index, info).  Lanes: nl test lanes per record."""
    s = OPS[op]
    key = (s["dom"], s["dom_a"], s["nl"], s["src"])
    if key in _rec_cache:
        return _rec_cache[key]
    import random
    rng = random.Random(99)
    dom, nl, src = s["dom"], s["nl"], s["src"]
    wb = DOM_W[dom] // 8
    wa = DOM_W[s["dom_a"]] // 8
    pairs, segs, B = pairs_for(dom)
    recs = bytearray()
    out_segs = []
    for name, lo, hi in segs:
        seg = pairs[lo:hi]
        if name == "boundary":
            if src != "ab":                                    # unary: just the boundary values, sliding window
                vals = [b for b in B] * 1
                seg = [(rng.getrandbits(DOM_W[dom]), v) for v in vals]
                # for the unary case also pair each boundary value with itself as 'a' (matters for nothing)
            # sliding window so every pair visits every lane position
            groups = [seg[(i + j) % len(seg)] for i in range(len(seg)) for j in range(nl)]
        else:
            if src != "ab":
                seg = seg[: len(seg)]
            groups = seg[: (len(seg) // nl) * nl]
        nrec = len(groups) // nl
        start = len(recs) // 32
        buf = bytearray(rng.randbytes(32 * nrec))
        for r in range(nrec):
            base = r * 32
            for j in range(nl):
                a, b = groups[r * nl + j]
                if src in ("ab", "a"):
                    buf[base + j * wa: base + (j + 1) * wa] = a.to_bytes(wa, "little")
                if src in ("ab", "b"):
                    buf[base + 16 + j * wb: base + 16 + (j + 1) * wb] = b.to_bytes(wb, "little")
        recs += buf
        out_segs.append((name, start, start + nrec))
    info = dict(lanes_per_rec=nl, n_rec=len(recs) // 32)
    _rec_cache[key] = (bytes(recs), out_segs, info)
    return _rec_cache[key]


# ---------------------------------------------------------------------------------------------
# FTZ oracle patch
# ---------------------------------------------------------------------------------------------
def ftz_patch(spec, recs, out, rc, daz):
    """Apply x86 FTZ (flush tiny results to a signed zero) on top of an emulator run that ignored it."""
    kind, fam, in_w, out_w, nl = spec["ftzm"]
    mb = afp_model.FMT[out_w]["p"] - 1
    minnorm = 1 << mb
    sign = 1 << (out_w - 1)
    ow, iw = out_w // 8, in_w // 8
    out = bytearray(out)
    n = len(recs) // 32
    patched = window = kept = 0
    unpack_in = {32: "<I", 64: "<Q"}[in_w]
    unpack_out = {32: "<I", 64: "<Q"}[out_w]
    for r in range(n):
        for j in range(nl):
            ro = r * 24 + j * ow
            res = struct.unpack_from(unpack_out, out, ro)[0]
            mag = res & (sign - 1)
            if mag == 0 or mag > minnorm:
                continue
            a = struct.unpack_from(unpack_in, recs, r * 32 + j * iw)[0]
            b = struct.unpack_from(unpack_in, recs, r * 32 + 16 + j * iw)[0]
            if daz:
                a = afp_model.flush_denormal_input(a, in_w)
                b = afp_model.flush_denormal_input(b, in_w)
            cb, _, vb = afp_model.decode(b, in_w)
            if cb != "fin":
                continue
            if kind == "bin":
                ca, _, va = afp_model.decode(a, in_w)
                if ca != "fin":
                    continue
                ex = afp_model.exact_binary(fam, va, vb)
            elif kind == "cvt":
                ex = vb
            elif kind == "rcp":
                if vb == 0:
                    continue
                ex = 1 / vb
            else:
                raise KeyError(kind)
            if ex is None:
                continue
            if afp_model.tiny_after_rounding(ex, out_w, rc):
                struct.pack_into(unpack_out, out, ro, res & sign)
                patched += 1
                if mag == minnorm:        # emulator rounded up to the smallest normal; x86 calls it tiny
                    window += 1
            elif mag == minnorm and abs(ex) < afp_model.Fraction(2) ** afp_model.FMT[out_w]["emin"]:
                kept += 1                 # below 2^emin exactly but rounds up to it: not tiny, must NOT be flushed
    return bytes(out), (patched, window, kept)


def nan_patch(spec, recs, out):
    """Intel SDM vol 1 table 4-7, SSE column: with a NaN in either source operand the result is the
    first source operand if it is a NaN (quieted), else the second (quieted).  QEMU 5.0 softfloat
    (hence Unicorn) applies the x87 rule instead (SNaN+QNaN -> the QNaN, two NaNs -> larger
    significand), which is not what SSE hardware does."""
    w = DOM_W[spec["dom"]]
    nl = spec["nl"]
    f = afp_model.FMT[w]
    mb = f["p"] - 1
    emask = (1 << f["ebits"]) - 1
    q = 1 << (mb - 1)
    fmt = {32: "<I", 64: "<Q"}[w]
    wb = w // 8
    out = bytearray(out)
    n = len(recs) // 32
    changed = 0
    isnan = lambda x: ((x >> mb) & emask) == emask and (x & ((1 << mb) - 1)) != 0
    for r in range(n):
        for j in range(nl):
            a = struct.unpack_from(fmt, recs, r * 32 + j * wb)[0]
            b = struct.unpack_from(fmt, recs, r * 32 + 16 + j * wb)[0]
            if isnan(a):
                want = a | q
            elif isnan(b):
                want = b | q
            else:
                continue
            o = r * 24 + j * wb
            if struct.unpack_from(fmt, out, o)[0] != want:
                struct.pack_into(fmt, out, o, want)
                changed += 1
    return bytes(out), changed


# ---------------------------------------------------------------------------------------------
# comparison
# ---------------------------------------------------------------------------------------------
def diff_records(a, b, stride=24, chunk=256):
    if a == b:
        return []
    n = len(a) // stride
    bad = []
    for s in range(0, n, chunk):
        e = min(n, s + chunk)
        if a[s * stride: e * stride] != b[s * stride: e * stride]:
            for i in range(s, e):
                if a[i * stride:(i + 1) * stride] != b[i * stride:(i + 1) * stride]:
                    bad.append(i)
    return bad


def classify(spec, recs, i):
    """Category of a record from its used input lanes."""
    dom, nl, src = spec["dom"], spec["nl"], spec["src"]
    cats = set()
    if dom.startswith("i"):
        return "int"
    w = DOM_W[dom]
    wb = w // 8
    f = afp_model.FMT[w]
    mb = f["p"] - 1
    emask = (1 << f["ebits"]) - 1
    unpack = "<I" if w == 32 else "<Q"
    offs = []
    for j in range(nl):
        if "b" in src:
            offs.append(16 + j * wb)
        if "a" in src:
            offs.append(j * wb)
    for o in offs:
        x = struct.unpack_from(unpack, recs, i * 32 + o)[0]
        e = (x >> mb) & emask
        m = x & ((1 << mb) - 1)
        if e == emask:
            cats.add("nan" if m else "inf")
        elif e == 0:
            cats.add("denormal" if m else "zero")
        elif e - f["bias"] >= (31 if w == 32 else 31) and e - f["bias"] <= 64:
            cats.add("big")
    for c in ("nan", "inf", "denormal", "big", "zero"):
        if c in cats:
            return c
    return "normal"


def out_lanes(op):
    """(lane width in bits, lanes) of the float result lanes in xmm0, or None when the result is not a float vector."""
    s = OPS[op]
    if op.startswith(("cmp", "comis", "ucomis")) or "2si" in op or "2dq" in op:
        return None
    return {"cvtsd2ss": (32, 1), "cvtpd2ps": (32, 2), "cvtss2sd": (64, 1), "cvtps2pd": (64, 2),
            "cvtsi2ss32": (32, 1), "cvtsi2ss64": (32, 1), "cvtsi2sd32": (64, 1), "cvtsi2sd64": (64, 1),
            "cvtdq2ps": (32, 4), "cvtdq2pd": (64, 2)}.get(op, (DOM_W[s["dom"]], s["nl"]))


def nonnan_mismatches(op, arm, ref, bad):
    """Of the mismatching records, how many differ in something other than a NaN result lane?
    (None when the op has no float result lanes.)"""
    ol = out_lanes(op)
    if ol is None:
        return None
    w, lanes = ol
    wb = w // 8
    f = afp_model.FMT[w]
    mb = f["p"] - 1
    emask = (1 << f["ebits"]) - 1
    fmt = {32: "<I", 64: "<Q"}[w]
    isnan = lambda x: ((x >> mb) & emask) == emask and (x & ((1 << mb) - 1)) != 0
    count = 0
    for i in bad:
        o = i * 24
        other = arm[o + lanes * wb: o + 24] != ref[o + lanes * wb: o + 24]       # junk lanes / rax must always be identical
        nonnan = other
        for j in range(lanes):
            x = struct.unpack_from(fmt, arm, o + j * wb)[0]
            y = struct.unpack_from(fmt, ref, o + j * wb)[0]
            if x != y and not (isnan(x) or isnan(y)):
                nonnan = True
        count += nonnan
    return count


def run_arm(variant, fpcr, recs):
    p = subprocess.run([EXE, "run", variant, f"{fpcr:x}"], input=recs, capture_output=True)
    if p.returncode != 0:
        raise RuntimeError(f"{variant} fpcr={fpcr:#x}: rc={p.returncode} {p.stderr.decode()}")
    return p.stdout


def variants_of(op):
    global _variant_list
    if "_variant_list" not in globals():
        txt = subprocess.run([EXE, "list"], capture_output=True, text=True, check=True).stdout
        _variant_list = [l.split()[0] for l in txt.splitlines()]
    c = OPS[op]["cname"]
    return [v for v in _variant_list if v.startswith(c + "__")]


# ---------------------------------------------------------------------------------------------
# worker
# ---------------------------------------------------------------------------------------------
_x86 = None


def _init(n_uniform, n_struct):
    global _x86
    Settings.n_uniform, Settings.n_struct = n_uniform, n_struct
    _x86 = X86()


def task(args):
    try:
        return _task(args)
    except Exception as e:                      # keep the pool alive; report the failure with the results
        import traceback
        return [dict(op=args[0], error=traceback.format_exc(), rc=args[1][0], daz=args[1][1], ftz=args[1][2])]


def _task(args):
    op, cfg = args
    rc, daz, ftz = cfg
    spec = OPS[op]
    t0 = time.time()
    recs, segs, info = records_for(op)
    mx = mxcsr_for(rc, daz, ftz)
    raw = _x86.run(op, recs, mx)
    ref, patched, nan_changed = raw, (0, 0, 0), 0
    if spec["name"][:3] in ("add", "sub", "mul", "div") and spec["name"][3:] in ("ss", "sd", "ps", "pd"):
        ref, nan_changed = nan_patch(spec, recs, ref)
    if ftz and spec["ftzm"]:
        ref, patched = ftz_patch(spec, recs, ref, rc, daz)
    results = []
    for variant in variants_of(op):
        for mode in modes_for(cfg):
            fpcr = fpcr_for(mode, rc, daz, ftz)
            arm = run_arm(variant, fpcr, recs)
            bad = diff_records(arm, ref)
            nonnan = nonnan_mismatches(op, arm, ref, bad)
            segcount = {}
            for name, lo, hi in segs:
                segcount[name] = sum(1 for i in bad if lo <= i < hi)
            cats = {}
            ex = []
            for i in bad:
                c = classify(spec, recs, i)
                cats[c] = cats.get(c, 0) + 1
            # examples: spread over categories
            seen = {}
            for i in bad:
                c = classify(spec, recs, i)
                if seen.get(c, 0) < 2 and len(ex) < 10:
                    seen[c] = seen.get(c, 0) + 1
                    ex.append(dict(cat=c, a=recs[i * 32:i * 32 + 16].hex(), b=recs[i * 32 + 16:i * 32 + 32].hex(),
                                   arm=arm[i * 24:i * 24 + 24].hex(), ref=ref[i * 24:i * 24 + 24].hex(),
                                   emu=raw[i * 24:i * 24 + 24].hex()))
            results.append(dict(op=op, variant=variant, mode=mode, rc=rc, daz=daz, ftz=ftz, fpcr=fpcr,
                                n_rec=info["n_rec"], lanes=info["lanes_per_rec"], bad=len(bad), bad_nonnan=nonnan, seg_bad=segcount,
                                seg_n={n: hi - lo for n, lo, hi in segs}, cats=cats, examples=ex,
                                ftz_patched=patched[0], ftz_window=patched[1], ftz_kept=patched[2], nan_patched=nan_changed, secs=round(time.time() - t0, 2)))
    return results


def match(name, pats):
    return not pats or any(fnmatch.fnmatch(name, p) for p in pats)


def cmd_build(_a=None):
    os.makedirs(OUT, exist_ok=True)
    srcs = [os.path.join(HERE, "afp_lab.c"), os.path.join(HERE, "afp_lab_errno.c")]
    objs = []
    for s, extra in zip(srcs, ([], ["-fmath-errno"])):
        o = os.path.join(OUT, os.path.basename(s)[:-2] + ".o")
        subprocess.run(["clang", "-O2", "-ffp-contract=off", "-Wall", "-Wextra", *extra, "-c", s, "-o", o], check=True)
        objs.append(o)
    subprocess.run(["clang", *objs, "-o", EXE], check=True)
    print("built", EXE)


def cmd_run(a):
    cmd_build()
    pats = a.ops.split(",") if a.ops else []
    tasks = [(op, cfg) for op in OPS if match(op, pats) for cfg in configs_for(op)]
    nu, ns = (8000, 5000) if a.quick else (Settings.n_uniform, Settings.n_struct)
    print(f"{len(tasks)} tasks, {a.jobs} workers, uniform={nu} structured={ns}", flush=True)
    t0 = time.time()
    allres = []
    def note(res):
        for r in res:
            if "error" in r:
                print("ERROR in", r["op"], (r["rc"], r["daz"], r["ftz"]), r["error"], flush=True)
    if a.jobs <= 1:                                            # in-process, for debugging
        _init(nu, ns)
        for k, t in enumerate(tasks):
            res = task(t); note(res); allres += res
    else:
        with Pool(a.jobs, initializer=_init, initargs=(nu, ns)) as pool:
            for k, res in enumerate(pool.imap_unordered(task, tasks, chunksize=1)):
                note(res)
                allres += res
                if (k + 1) % 25 == 0:
                    print(f"  {k + 1}/{len(tasks)} tasks, {time.time() - t0:.0f}s", flush=True)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w") as f:
        json.dump(dict(settings=dict(n_uniform=nu, n_struct=ns), results=allres), f)
    print(f"done: {len(allres)} (op,variant,mode,config) results in {time.time() - t0:.0f}s -> {a.out}")


def cfg_name(r):
    s = f"rc{r['rc']}"
    if r["daz"]:
        s += "+daz"
    if r["ftz"]:
        s += "+ftz"
    return s


def cmd_summary(a):
    data = json.load(open(a.file))
    pats = a.ops.split(",") if a.ops else []
    by = {}
    for r in data["results"]:
        if "error" in r:
            print("ERROR", r["op"], r["error"][-300:])
            continue
        if not match(r["op"], pats):
            continue
        by.setdefault((r["op"], r["variant"]), {}).setdefault(r["mode"], []).append(r)
    last = None
    for (op, variant), modes in sorted(by.items()):
        if op != last:
            print()
            last = op
        cells = []
        for mode in ("def", "ah", "ah_nep", "fiz"):
            rs = modes.get(mode)
            if not rs:
                continue
            nbad = [r for r in rs if r["bad"]]
            if not nbad:
                cells.append(f"{mode}: exact({len(rs)})")
            else:
                tot = sum(r["bad"] for r in nbad)
                cats = {}
                for r in nbad:
                    for c, n in r["cats"].items():
                        cats[c] = cats.get(c, 0) + n
                bad_cfgs = ",".join(cfg_name(r) for r in sorted(nbad, key=cfg_name))
                cells.append(f"{mode}: NOT {len(nbad)}/{len(rs)} cfg [{bad_cfgs if len(bad_cfgs) < 60 else bad_cfgs[:57] + '...'}] {tot} rec {cats}")
        print(f"{op:13s} {variant.split('__')[1]:11s} " + " | ".join(cells))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("build")
    r = sub.add_parser("run")
    r.add_argument("--ops", default="")
    r.add_argument("--jobs", type=int, default=8)
    r.add_argument("--quick", action="store_true")
    r.add_argument("--out", default=os.path.join(OUT, "results.json"))
    s = sub.add_parser("summary")
    s.add_argument("--file", default=os.path.join(OUT, "results.json"))
    s.add_argument("--ops", default="")
    a = ap.parse_args()
    {"build": cmd_build, "run": cmd_run, "summary": cmd_summary}[a.cmd](a)
