#!/usr/bin/env python3
"""Known-answer tables and small measurements quoted in docs/findings/11-float-mode.md."""
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import afp_lab                                   # noqa: E402
from afp_lab import mxcsr_for, fpcr_for, run_arm, OPS   # noqa: E402
from afp_x86 import X86                          # noqa: E402

x86 = X86()
JUNK = bytes(range(0x40, 0x50))


def rec32(a, b):
    return struct.pack("<I", a) + JUNK[4:16] + struct.pack("<I", b) + JUNK[4:16]


def rec64(a, b):
    return struct.pack("<Q", a) + JUNK[8:16] + struct.pack("<Q", b) + JUNK[8:16]


def lane0(out, w=32, off=0):
    return struct.unpack_from("<I" if w == 32 else "<Q", out, off)[0]


def one(op, variant, recs, rc=0, daz=0, ftz=0, mode="def"):
    """Return (x86 oracle result bytes, arm result bytes) for the single record(s)."""
    mx = mxcsr_for(rc, daz, ftz)
    ref = x86.run(op, recs, mx)
    arm = run_arm(variant, fpcr_for(mode, rc, daz, ftz), recs)
    return ref, arm


def f32(b):
    return struct.unpack("<f", struct.pack("<I", b))[0]


print("### 1. the seven README cases, through the harness (lane 0, hex)")
cases = [
    ("inf - inf", "subss", "subss__c", 0x7f800000, 0x7f800000),
    ("qNaN(1) * sNaN(2)", "mulss", "mulss__c", 0x7fc11111, 0x7f822222),
    ("min(qNaN, 1.0)", "minss", "minss__fmin", 0x7fc11111, 0x3f800000),
    ("min(1.0, qNaN)", "minss", "minss__fmin", 0x3f800000, 0x7fc11111),
    ("min(+0, -0)", "minss", "minss__fmin", 0x00000000, 0x80000000),
    ("max(-0, +0)", "maxss", "maxss__fmin", 0x80000000, 0x00000000),
]
print(f"{'case':20s} {'x86 (Unicorn)':>14s} {'ARM default':>12s} {'ARM AH':>10s}")
for label, op, var, a, b in cases:
    r = rec32(a, b)
    ref = x86.run(op, r, 0x1f80)
    d = run_arm(var, fpcr_for("def", 0, 0, 0), r)
    h = run_arm(var, fpcr_for("ah", 0, 0, 0), r)
    print(f"{label:20s} {lane0(ref):14x} {lane0(d):12x} {lane0(h):10x}")

print("\n### 2. comiss / ucomiss flags (ZF PF CF), rflags & 0x8d5; ordering of a (xmm0) vs b (xmm1)")
orders = [("a > b", 0x40000000, 0x3f800000), ("a < b", 0x3f800000, 0x40000000), ("a == b", 0x3f800000, 0x3f800000),
          ("+0 == -0", 0, 0x80000000), ("unordered (qNaN)", 0x7fc00000, 0x3f800000), ("unordered (b sNaN)", 0x3f800000, 0x7f800001),
          ("unordered (both)", 0x7fc00000, 0x7fc00000)]
for op in ("comiss", "ucomiss"):
    for label, a, b in orders:
        r = rec32(a, b)
        ref = x86.run(op, r, 0x1f80)
        res = []
        for var, mode in ((op + "__c", "def"), (op + "__c", "ah"), (op + "__nzcv", "def"), (op + "__nzcv", "ah")) if op == "comiss" else ((op + "__c", "def"), (op + "__c", "ah")):
            res.append(struct.unpack_from("<Q", run_arm(var, fpcr_for(mode, 0, 0, 0), r), 16)[0])
        fl = struct.unpack_from("<Q", ref, 16)[0]
        names = "".join(n for n, bit in (("Z", 0x40), ("P", 4), ("C", 1), ("O", 0x800), ("S", 0x80), ("A", 0x10)) if fl & bit) or "-"
        print(f"  {op:8s} {label:20s} x86 flags={fl:#05x} ({names})  arm C/def,C/ah{',nzcv/def,nzcv/ah' if op == 'comiss' else ''}: {[hex(x) for x in res]}  {'OK' if all(x == fl for x in res) else 'MISMATCH'}")

print("\n### 3. min/max cases: x86 returns the SECOND operand unless a < b (minss)")
mm = [("min(1,2)", 0x3f800000, 0x40000000), ("min(2,1)", 0x40000000, 0x3f800000), ("min(NaN,1)", 0x7fc00001, 0x3f800000), ("min(1,NaN)", 0x3f800000, 0x7fc00001),
      ("min(sNaN,qNaN2)", 0x7f800001, 0x7fc00002), ("min(+0,-0)", 0, 0x80000000), ("min(-0,+0)", 0x80000000, 0),
      ("min(denorm,+inf), DAZ", 1, 0x7f800000)]
for label, a, b in mm:
    r = rec32(a, b)
    daz = 1 if "DAZ" in label else 0
    ref = x86.run("minss", r, mxcsr_for(0, daz, 0))
    outs = []
    for var, mode in (("minss__ternary", "def"), ("minss__ternary", "ah"), ("minss__fmin", "def"), ("minss__fmin", "ah"), ("minss__libm", "ah")):
        outs.append(f"{var.split('__')[1]}/{mode}={lane0(run_arm(var, fpcr_for(mode, 0, daz, 0), r)):08x}")
    print(f"  {label:24s} x86={lane0(ref):08x}  " + "  ".join(outs))

print("\n### 4. rcpss / rsqrtss: emulator vs exact vs native estimates (default FPCR | AH)")
vals = [0x3f800000, 0x40000000, 0x40400000, 0x3f000000, 0x41200000, 0x40490fdb, 0x3fc00000]
print(f"{'x':>10s} {'emulator rcpss':>16s} {'1.0f/x':>12s} {'frecpe def':>12s} {'frecpe AH':>12s} {'frecpe+NR AH':>13s} || {'emu rsqrtss':>12s} {'frsqrte def':>12s} {'frsqrte AH':>11s}")
for v in vals:
    r = rec32(0, v)
    e1 = lane0(x86.run("rcpss", r, 0x1f80)); e2 = lane0(x86.run("rsqrtss", r, 0x1f80))
    row = [lane0(run_arm("rcpss__div", 0, r), 32, 0), lane0(run_arm("rcpss__frecpe", 0, r)), lane0(run_arm("rcpss__frecpe", 2, r)), lane0(run_arm("rcpss__frecpe_nr", 2, r))]
    q = [lane0(run_arm("rsqrtss__frsqrte", 0, r)), lane0(run_arm("rsqrtss__frsqrte", 2, r))]
    print(f"{f32(v):10.6g} {e1:16x} {row[0]:12x} {row[1]:12x} {row[2]:12x} {row[3]:13x} || {e2:12x} {q[0]:12x} {q[1]:11x}")
rng = random.Random(5)
vs = [(rng.getrandbits(1) << 31) * 0 | (rng.randint(60, 190) << 23) | rng.getrandbits(23) for _ in range(20000)]
recs = b"".join(rec32(0, v) for v in vs)
emu_rcp = x86.run("rcpss", recs, 0x1f80); emu_rsq = x86.run("rsqrtss", recs, 0x1f80)
print("stats over 20000 random positive normals (relative error of lane 0 against the exact value, in units of 2^-12):")
for label, var, fp, emu, exact in (("frecpe default", "rcpss__frecpe", 0, emu_rcp, lambda x: 1.0 / x), ("frecpe AH (FEAT_RPRES)", "rcpss__frecpe", 2, emu_rcp, lambda x: 1.0 / x),
                                   ("frecpe+1 Newton AH", "rcpss__frecpe_nr", 2, emu_rcp, lambda x: 1.0 / x),
                                   ("frsqrte default", "rsqrtss__frsqrte", 0, emu_rsq, lambda x: x ** -0.5), ("frsqrte AH (FEAT_RPRES)", "rsqrtss__frsqrte", 2, emu_rsq, lambda x: x ** -0.5),
                                   ("frsqrte+1 Newton AH", "rsqrtss__frsqrte_nr", 2, emu_rsq, lambda x: x ** -0.5)):
    out = run_arm(var, fp, recs)
    same = sum(1 for i in range(len(vs)) if lane0(out, 32, i * 24) == lane0(emu, 32, i * 24))
    worst = max(abs(f32(lane0(out, 32, i * 24)) / exact(f32(vs[i])) - 1) for i in range(len(vs)))
    print(f"  {label:26s} equal to emulator: {same:6d}/{len(vs)}   max relative error {worst * 4096:.3f} x 2^-12")

print("\n### 5. FTZ: Unicorn ignores MXCSR.FTZ for SSE (bit 15)")
r = rec32(0x00800000, 0x3f000000)   # min normal * 0.5 = 2^-127, exact denormal
for mx in (0x1f80, 0x9f80):
    print(f"  mulss 2^-126 * 0.5 with MXCSR={mx:#x}: Unicorn lane0 = {lane0(x86.run('mulss', r, mx)):08x} (hardware with FTZ=1: 00000000)")

print("\n### 6. tininess: product just below 2^-126 that rounds up to it (window cases found by the generator)")
import afp_model
from afp_inputs import Gen
g = Gen("f32", 777)
shown = 0
for _ in range(200000):
    a, b = g.pair_float()
    ca, sa, va = afp_model.decode(a, 32); cb, sb, vb = afp_model.decode(b, 32)
    if ca != "fin" or cb != "fin" or va == 0 or vb == 0:
        continue
    ex = va * vb
    if ex != 0 and abs(ex) < afp_model.Fraction(2) ** -126 and not afp_model.tiny_after_rounding(ex, 32, 0) and afp_model.encode_round(ex, 32, 0) & 0x7fffffff == 0x00800000:
        r = rec32(a, b)
        ref, armdef = one("mulss", "mulss__c", r, 0, 0, 1, "def")
        _, armah = one("mulss", "mulss__c", r, 0, 0, 1, "ah")
        print(f"  mulss {a:08x} * {b:08x}: exact product is {float(abs(ex)) / 2.0**-126:.12f} x 2^-126; x86 (FTZ=1, tiny after rounding? no) -> {lane0(ref):08x}; "
              f"ARM FZ without AH -> {lane0(armdef):08x}; ARM FZ with AH -> {lane0(armah):08x}")
        shown += 1
        if shown >= 3:
            break

print("\n### 7. float -> int conversions x86 vs plain ARM (cvttss2si eax / rax)")
for label, v in (("NaN", 0x7fc00000), ("+inf", 0x7f800000), ("2^31", 0x4f000000), ("2^31-128", 0x4effffff), ("-2^31", 0xcf000000), ("-2^31-256", 0xcf000001), ("2^63", 0x5f000000), ("-2^63", 0xdf000000), ("1.9", 0x3ff33333), ("-1.9", 0xbff33333)):
    r = rec32(0, v)
    row = []
    for op in ("cvttss2si32", "cvttss2si64"):
        ref = struct.unpack_from("<Q", x86.run(op, r, 0x1f80), 16)[0]
        nv = struct.unpack_from("<Q", run_arm(op + "__naive", fpcr_for("ah", 0, 0, 0), r), 16)[0]
        ck = struct.unpack_from("<Q", run_arm(op + "__checked", fpcr_for("ah", 0, 0, 0), r), 16)[0]
        row.append(f"{op[-2:]}: x86={ref:#x} naive={nv:#x} checked={ck:#x}")
    print(f"  {label:10s} " + " | ".join(row))
