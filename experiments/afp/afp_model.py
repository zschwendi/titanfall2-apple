"""Exact (Fraction based) IEEE-754 binary32/binary64 model, used for two things only:

1. tininess-after-rounding, to turn Unicorn's output into what x86 hardware does with MXCSR.FTZ=1.
   Unicorn 2.1.4 (QEMU 5.0 softfloat) ignores MXCSR.FTZ for SSE entirely (measured, see the
   write-up), so for FTZ=1 the oracle is "Unicorn with FTZ ignored, then flush every result whose
   exact value is tiny after rounding to the destination precision (Intel SDM vol 1, 10.2.3.3 and
   4.9.1.5), keeping the sign".  This is a model of the manual, not a measurement of hardware.
2. an independent check that Unicorn's add/sub/mul/div/sqrt rounding agrees with exact IEEE
   arithmetic in all four MXCSR rounding modes (selfcheck at the bottom).
"""
import math
from fractions import Fraction

FMT = {
    32: dict(w=32, p=24, emin=-126, emax=127, bias=127, ebits=8),
    64: dict(w=64, p=53, emin=-1022, emax=1023, bias=1023, ebits=11),
}
RN, RM, RP, RZ = 0, 1, 2, 3          # MXCSR.RC encoding: 0 nearest, 1 down, 2 up, 3 toward zero


def decode(bits, w):
    """-> (cls, sign, Fraction); cls in 'fin','inf','nan'."""
    f = FMT[w]
    sign = bits >> (w - 1)
    ex = (bits >> (f["p"] - 1)) & ((1 << f["ebits"]) - 1)
    fr = bits & ((1 << (f["p"] - 1)) - 1)
    if ex == (1 << f["ebits"]) - 1:
        return ("nan" if fr else "inf"), sign, None
    if ex == 0:
        v = Fraction(fr) * Fraction(2) ** (f["emin"] - (f["p"] - 1))
    else:
        v = Fraction(fr + (1 << (f["p"] - 1))) * Fraction(2) ** (ex - f["bias"] - (f["p"] - 1))
    return "fin", sign, -v if sign else v


def is_denormal(bits, w):
    f = FMT[w]
    return ((bits >> (f["p"] - 1)) & ((1 << f["ebits"]) - 1)) == 0 and (bits & ((1 << (f["p"] - 1)) - 1)) != 0


def floor_log2(a):
    """floor(log2(a)) for a positive Fraction."""
    n, d = a.numerator, a.denominator
    e = n.bit_length() - d.bit_length()
    # a in [2^(e-1), 2^(e+1)); fix up
    if e >= 0:
        if n < d << e:
            e -= 1
    else:
        if n << -e < d:
            e -= 1
    return e


def _round_int(m, sign, rc):
    """m: positive Fraction, round to integer per mode; returns int."""
    fl = m.numerator // m.denominator
    rem = m - fl
    if rem == 0:
        return fl
    if rc == RN:
        if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and (fl & 1)):
            return fl + 1
        return fl
    if rc == RZ:
        return fl
    if rc == RP:
        return fl + (0 if sign else 1)
    return fl + (1 if sign else 0)      # RM


def round_unbounded(v, p, rc):
    """Round nonzero Fraction v to p significant bits with unbounded exponent range."""
    sign = v < 0
    a = abs(v)
    e = floor_log2(a)
    scale = Fraction(2) ** (e - (p - 1))
    mant = _round_int(a / scale, sign, rc)
    r = mant * scale
    return -r if sign else r


def tiny_after_rounding(v, w, rc):
    """x86 'tiny' test: nonzero exact result whose rounding (unbounded exponent) is below 2^emin."""
    if v == 0:
        return False
    f = FMT[w]
    r = round_unbounded(v, f["p"], rc)
    return abs(r) < Fraction(2) ** f["emin"]


def encode_round(v, w, rc, zero_sign=0):
    """Correctly rounded bits of Fraction v (finite) with gradual underflow and overflow."""
    f = FMT[w]
    p, emin, emax, bias = f["p"], f["emin"], f["emax"], f["bias"]
    if v == 0:
        return zero_sign << (w - 1)
    sign = 1 if v < 0 else 0
    a = abs(v)
    e = floor_log2(a)
    q = max(e, emin)
    scale = Fraction(2) ** (q - (p - 1))
    mant = _round_int(a / scale, bool(sign), rc)
    if mant >= (1 << p):
        mant >>= 1
        q += 1
    if q > emax:
        inf = ((1 << f["ebits"]) - 1) << (p - 1)
        maxf = inf - 1
        to_inf = (rc == RN) or (rc == RP and not sign) or (rc == RM and sign)
        return (sign << (w - 1)) | (inf if to_inf else maxf)
    if mant < (1 << (p - 1)):
        return (sign << (w - 1)) | mant
    return (sign << (w - 1)) | ((q + bias) << (p - 1)) | (mant - (1 << (p - 1)))


def sqrt_fraction(v, extra=80):
    """sqrt of nonnegative Fraction as a Fraction good for rounding decisions (sticky folded in)."""
    if v == 0:
        return Fraction(0)
    # scale so the integer sqrt has plenty of bits
    k = max(0, (extra * 2 - (v.numerator.bit_length() - v.denominator.bit_length())) // 2 + 1)
    scaled = v * Fraction(4) ** k
    t = scaled.numerator // scaled.denominator
    s = math.isqrt(t)
    inexact = (Fraction(s * s) != scaled)
    return (Fraction(s) + (Fraction(1, 2) if inexact else 0)) / Fraction(2) ** k


def exact_binary(op, x, y):
    if op == "add":
        return x + y
    if op == "sub":
        return x - y
    if op == "mul":
        return x * y
    if op == "div":
        return None if y == 0 else x / y
    raise KeyError(op)


def flush_denormal_input(bits, w):
    f = FMT[w]
    return bits & (1 << (w - 1)) if is_denormal(bits, w) else bits


# ---------------------------------------------------------------------------------------------
if __name__ == "__main__":
    # selfcheck: Unicorn arithmetic (all four rounding modes) against the exact model
    import random, struct, sys
    sys.path.insert(0, __file__.rsplit("/", 1)[0])
    from afp_x86 import X86
    x86 = X86()
    rng = random.Random(1234)
    total_bad = 0
    for w, sfx in ((32, "ss"), (64, "sd")):
        f = FMT[w]
        mask = (1 << w) - 1
        pk = "<I" if w == 32 else "<Q"
        def rnd():
            m = rng.random()
            if m < 0.4:
                return rng.getrandbits(w)
            e = rng.choice([0, 1, 2, f["bias"] - 30, f["bias"] - 1, f["bias"], f["bias"] + 1, f["bias"] + 30, (1 << f["ebits"]) - 2])
            return (rng.getrandbits(1) << (w - 1)) | (e << (f["p"] - 1)) | rng.getrandbits(f["p"] - 1)
        for op in ("add", "sub", "mul", "div", "sqrt"):
            pairs = [(rnd(), rnd()) for _ in range(30000)]
            for rc in (RN, RM, RP, RZ):
                recs = b"".join(struct.pack(pk, a) + b"\0" * (16 - w // 8) + struct.pack(pk, b) + b"\0" * (16 - w // 8) for a, b in pairs)
                out = x86.run(op + sfx, recs, 0x1f80 | (rc << 13))
                bad = checked = 0
                for i, (a, b) in enumerate(pairs):
                    got = struct.unpack_from(pk, out, i * 24)[0]
                    ca, sa, va = decode(a, w)
                    cb, sb, vb = decode(b, w)
                    if op == "sqrt":
                        if cb != "fin" or sb or vb == 0:
                            continue
                        ex = sqrt_fraction(vb)
                    else:
                        if ca != "fin" or cb != "fin":
                            continue
                        ex = exact_binary(op, va, vb)
                        if ex is None or ex == 0:
                            continue
                    want = encode_round(ex, w, rc)
                    checked += 1
                    if want != got:
                        bad += 1
                        if bad <= 3:
                            print("MISMATCH", op + sfx, "rc", rc, hex(a), hex(b), "emu", hex(got), "model", hex(want))
                total_bad += bad
                print(f"{op + sfx:7s} rc={rc}: {checked} finite non-zero results checked, {bad} differ from exact model")
    print("TOTAL differences:", total_bad)
