"""Test inputs for the AFP experiment: boundary sets, uniform random, and structured random.

All generators are deterministic (seeded) and shared by every op of the same operand domain, so
two ops are always compared on the same operands.  Domains: f32, f64, i32, i64 (raw bit patterns).
"""
import random
import struct
from fractions import Fraction
from afp_model import FMT, encode_round, RN

DOM_W = {"f32": 32, "f64": 64, "i32": 32, "i64": 64}


def f32_bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def f64_bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def boundary(dom):
    """Sorted list of boundary bit patterns for a domain."""
    w = DOM_W[dom]
    S = set()
    mask = (1 << w) - 1
    if dom in ("f32", "f64"):
        f = FMT[w]
        mb, eb = f["p"] - 1, f["ebits"]
        sign = 1 << (w - 1)
        emax_field = (1 << eb) - 1
        inf = emax_field << mb
        qbit = 1 << (mb - 1)
        fb = f32_bits if w == 32 else f64_bits
        for s in (0, sign):
            S.add(s | 0)                                             # +-0
            S.add(s | inf)                                           # +-inf
            for p in (0, 1, 0x12345, 0x155555, 0x2aaaaa, qbit - 1):  # quiet NaN, assorted payloads
                S.add(s | inf | qbit | (p & (qbit - 1)))
            S.add(s | inf | (qbit - 1) | qbit)                       # all-ones payload
            for p in (1, 2, 0x12345, 0x155555, 0x2aaaaa, qbit >> 1, qbit - 1):   # signalling NaN
                S.add(s | inf | (p & (qbit - 1)) | (0 if p else 1))
            for m in (1, 2, 3, 0xfff, qbit >> 1, qbit - 1, qbit, qbit + 1, (1 << mb) - 2, (1 << mb) - 1):   # denormals
                S.add(s | m)
            for b in (1 << mb, (1 << mb) + 1, (1 << mb) + 2, (2 << mb) - 1, 2 << mb,              # smallest normals
                      inf - 1, inf - 2, inf - (1 << mb), inf - (1 << mb) - 1):                    # largest normals
                S.add(s | b)
            for v in ([1.0, 1.5, 2.0, 2.5, 3.5, 0.5, 0.25, 0.1, 0.3, 10.0, 100.0, 1e10, 1e-10, 1e30, 1e-30, 3.0, 7.0,
                       0.49999997, 0.50000006, 1.4999999, 2.4999998, 8388607.5, 8388608.0, 8388609.0, 16777215.0,
                       16777216.0, 16777218.0, 4294967296.0, 18446744073709551616.0, 1.17549435e-38, 3.4028235e+38]
                      if w == 32 else
                      [1.0, 1.5, 2.0, 2.5, 3.5, 0.5, 0.25, 0.1, 0.3, 10.0, 100.0, 1e10, 1e-10, 1e300, 1e-300, 3.0, 7.0,
                       0.49999999999999994, 0.5000000000000001, 1.4999999999999998, 2.4999999999999996,
                       4503599627370495.5, 4503599627370496.0, 4503599627370497.0, 9007199254740991.0,
                       9007199254740992.0, 9007199254740994.0, 4294967296.0, 18446744073709551616.0,
                       2147483647.0, 2147483647.5, 2147483647.9999998, 2147483648.0, 2147483648.5,
                       2147483649.0, 2147483648.9999998,
                       1e-310, 2.2250738585072014e-308, 1.7976931348623157e+308,
                       # float32 narrowing boundaries
                       3.4028234663852886e+38, 3.4028235677973362e+38, 3.4028235677973366e+38, 3.5e38,
                       1.1754943508222875e-38, 1.1754942106924411e-38, 1.401298464324817e-45,
                       7.006492321624085e-46, 7.006492321624087e-46, 2.8e-45, 1e-46]):
                S.add(fb(v) | s)
            # 2^31, 2^32, 2^63, 2^64 and one ulp either side (the integer-conversion limits)
            for e in (31, 32, 63, 64):
                if w == 32 or e < 1023:
                    c = ((e + f["bias"]) << mb)
                    for d in (-2, -1, 0, 1, 2):
                        S.add(s | (c + d))
        if w == 32:
            for e in (31, 63):                                        # -2^31 / -2^63 exact and neighbours
                c = ((e + 127) << mb)
                for d in (-1, 0, 1):
                    S.add(sign | (c + d))
        else:
            for v in (-2147483648.0, -2147483648.5, -2147483649.0, -2147483649.5, -2147483647.5, -9223372036854775808.0):
                S.add(f64_bits(v))
            c = (63 + 1023) << mb
            for d in (-3, -2, -1, 0, 1, 2, 3):
                S.add(sign | (c + d))
    else:
        top = 1 << (w - 1)
        base = [0, 1, 2, 3, 7, 8, 15, 16, 255, 256, 65535, 65536, (1 << 24) - 1, 1 << 24, (1 << 24) + 1, (1 << 24) + 2,
                (1 << 24) + 3, (1 << 25) + 1, (1 << 25) + 2, (1 << 25) + 3, (1 << 25) + 6, 0x7fffff80, 0x7fffffc0,
                0x7fffffbf, 0x7fffffc1, 0x7fffff7f, 0x7ffffff0, top - 1, top - 2, top, top + 1,
                0x01000001, 0x03000003, 0x0aaaaaaa, 0x55555555, 0x12345678]
        if w == 64:
            base += [(1 << 53) - 1, 1 << 53, (1 << 53) + 1, (1 << 53) + 2, (1 << 53) + 3, (1 << 54) + 2, (1 << 54) + 6,
                     (1 << 62) + 512, (1 << 62) + 511, (1 << 62) + 513, (1 << 62) + 1024, 0x7ffffffffffffc00,
                     0x7ffffffffffffe00, 0x7fffffffffffff80, (1 << 40) + (1 << 16), (1 << 40) + (1 << 16) + 1,
                     (1 << 40) + (1 << 16) - 1, (1 << 32) - 1, 1 << 32, (1 << 32) + 1, 0xffffffffffffff00,
                     0x7ffffff000000000]
        for v in base:
            S.add(v & mask)
            S.add((-v) & mask)
    return sorted(S)


class Gen:
    def __init__(self, dom, seed):
        self.dom = dom
        self.w = DOM_W[dom]
        self.rng = random.Random(seed)
        self.mask = (1 << self.w) - 1
        self.isfloat = dom.startswith("f")
        if self.isfloat:
            f_ = FMT[self.w]
            self.mb = f_["p"] - 1
            self.eb = f_["ebits"]
            self.emask = (1 << self.eb) - 1
            self.bias = f_["bias"]
            self.p = f_["p"]
            self.sign = 1 << (self.w - 1)
        self.B = boundary(dom)

    # ----- helpers
    def unif(self):
        return self.rng.getrandbits(self.w)

    def make(self, s, e, m):
        return (s << (self.w - 1)) | (e << self.mb) | m

    def expo(self, x):
        return (x >> self.mb) & self.emask

    def mutate(self, x):
        r, rng = self.rng, self.rng
        c = r.randrange(9)
        if c == 0:
            return x
        if c == 1:
            return x ^ self.sign
        if c == 2:
            return (x + rng.randrange(-4, 5)) & self.mask
        if c == 3:
            return x ^ (1 << rng.randrange(self.w))
        if c == 4:
            return (x & ~((1 << self.mb) - 1)) | rng.getrandbits(self.mb)
        if c == 5:
            return ((x ^ self.sign) + rng.randrange(-3, 4)) & self.mask
        if c == 6:
            return self.unif()
        if c == 7:                                           # new NaN payload / quiet bit
            return (x & ~((1 << self.mb) - 1)) | (rng.getrandbits(self.mb))
        return x ^ (1 << (self.mb - 1))                       # flip the quiet bit

    def rand_normal_exp(self, lo, hi, s=None):
        r = self.rng
        return self.make(r.getrandbits(1) if s is None else s, r.randint(lo, hi), r.getrandbits(self.mb))

    def nearest(self, frac):
        return encode_round(frac, self.w, RN)

    # ----- one structured float pair
    def pair_float(self):
        rng, w, mb, em = self.rng, self.w, self.mb, self.emask
        B = self.B
        m = rng.randrange(16 if self.w == 64 else 14)
        sg = lambda: rng.getrandbits(1)
        if m == 0:
            x = rng.choice(B); return x, self.mutate(x)
        if m == 1:
            x = self.unif(); return x, self.mutate(x)
        if m == 2:                                            # close exponents: cancellation, alignment
            ex = rng.randint(1, em - 1); ey = min(em - 1, max(1, ex + rng.randint(-3, 3)))
            return self.rand_normal_exp(ex, ex), self.rand_normal_exp(ey, ey)
        if m == 3:
            return rng.choice(B), self.unif()
        if m == 4:
            return self.unif(), rng.choice(B)
        if m == 5:                                            # exact halfway cases for add/sub rounding
            ex = rng.randint(self.p + 1, em - 1)
            x = self.make(sg(), ex, rng.getrandbits(mb))
            ey = ex - rng.choice([self.p, self.p - 1, self.p + 1, self.p])
            y = self.make(sg(), ey, rng.choice([0, 0, 1, rng.getrandbits(mb)]))
            return (x, y) if rng.random() < 0.5 else (y, x)
        if m == 6:                                            # integers and halves: rounding to integer
            def half():
                lim = min(self.mb - 1, 52)
                k = rng.getrandbits(rng.randint(1, lim)) * (1 if sg() else -1)
                v = k + rng.choice([0.0, 0.5, 0.5, 0.25, -0.5])
                return f32_bits(v) if self.w == 32 else f64_bits(v)
            return half(), half()
        if m == 7:                                            # tiny exponents: denormals and the normal edge
            ex = rng.choice([0, 0, 1, 2, 3]); ey = rng.choice([0, 1, 2, self.bias - 1, self.bias, self.bias + 1])
            return self.make(sg(), ex, rng.getrandbits(mb)), self.make(sg(), ey, rng.getrandbits(mb))
        if m == 8:                                            # mul window: x*y within a few ulp of 2^emin
            e1 = rng.randint(-(self.bias - 2), 0) + self.bias
            x = self.make(sg(), e1, rng.getrandbits(mb))
            _, sx, vx = self.dec(x)
            y = self.nearest(Fraction(2) ** (1 - self.bias) / abs(vx))
            y = (y + rng.randrange(-3, 4)) & self.mask
            return (x, y ^ (self.sign if rng.random() < 0.5 else 0))
        if m == 9:                                            # div window: x/y within a few ulp of 2^emin
            e2 = rng.randint(self.bias, em - 2)
            y = self.make(sg(), e2, rng.getrandbits(mb))
            _, sy, vy = self.dec(y)
            x = self.nearest(Fraction(2) ** (1 - self.bias) * abs(vy))
            x = (x + rng.randrange(-3, 4)) & self.mask
            return (x ^ (self.sign if rng.random() < 0.5 else 0), y)
        if m == 10:                                           # NaNs of every flavour against anything
            def nan():
                return self.make(sg(), em, rng.choice([rng.getrandbits(mb) | 1, (1 << (mb - 1)) | rng.getrandbits(mb - 1), 1]))
            a = nan()
            return (a, nan()) if rng.random() < 0.5 else ((a, self.unif()) if rng.random() < 0.5 else (self.unif(), a))
        if m == 11:                                           # infinities and zeros against anything finite or not
            z = self.make(sg(), rng.choice([0, em]), 0)
            return (z, self.unif()) if rng.random() < 0.5 else (self.unif(), z)
        if m == 12:                                           # large magnitudes: overflow, 2^31 / 2^63 neighbourhoods
            e = rng.choice([31, 32, 63, 64, 127 if w == 32 else 1023]) + self.bias
            return self.make(sg(), min(em - 1, e + rng.randint(-1, 1)), rng.getrandbits(mb)), self.unif()
        if m == 13:                                           # ulp neighbours of exact integer-valued floats
            k = rng.getrandbits(rng.randint(1, 62)) * (1 if sg() else -1)
            v = float(k)
            x = (f32_bits(v) if w == 32 else f64_bits(v)) if abs(v) < 3.4e38 else self.unif()
            return (x + rng.randrange(-2, 3)) & self.mask, self.unif()
        # w == 64 only: narrowing window (cvtsd2ss / cvtpd2ps) around 2^-126 and the float32 overflow edge
        if m == 14:
            base = Fraction(2) ** -126 * (1 + Fraction(rng.choice([-1, 1]), 1) * Fraction(rng.getrandbits(8) + 1, 1 << rng.randint(24, 40)))
            if rng.random() < 0.5:
                base = Fraction(2) ** -126 * (1 - Fraction(rng.getrandbits(6) + 1, 1 << rng.randint(25, 33)))
            if rng.random() < 0.3:
                base = Fraction(rng.randint(1, 1 << 24), 1 << rng.randint(149, 150)) + Fraction(rng.getrandbits(5), 1 << 190)
            if rng.random() < 0.5:
                base = -base
            return self.nearest(base), self.nearest(base * (1 + Fraction(1, 1 << 40)))
        base = (Fraction(2) ** 128 - Fraction(2) ** 103) * (1 + Fraction(rng.randint(-5, 5), 1 << rng.randint(40, 52)))
        if rng.random() < 0.5:
            base = -base
        return self.nearest(base), self.nearest(base * Fraction(rng.choice([1, 2, 3]), 3))

    def dec(self, bits):
        from afp_model import decode
        return decode(bits, self.w)

    # ----- one structured int pair
    def pair_int(self):
        rng, w = self.rng, self.w
        def one():
            c = rng.randrange(7)
            if c == 0:
                v = rng.getrandbits(rng.randint(1, w))
            elif c == 1 or c == 2:                            # exact and near ties for rounding to binary32 / binary64
                p = 24 if c == 1 else 53
                if w == 32 and p == 53:
                    return rng.getrandbits(w)
                L = rng.randint(p + 1, w - 1)
                s = L - p
                v = ((rng.getrandbits(p - 1) | (1 << (p - 1))) << s) + (1 << (s - 1)) + rng.choice([0, 0, 1, -1])
            elif c == 3:
                v = rng.randint(-16, 16)
            elif c == 4:
                k = rng.randint(1, w - 1)
                v = (1 << k) + rng.randint(-3, 3)
            elif c == 5:
                v = (1 << rng.randint(1, w - 1)) - 1
            else:
                return rng.getrandbits(w)
            if rng.random() < 0.5:
                v = -v
            return v & self.mask
        return one(), one()

    def uniform_pairs(self, n):
        return [(self.unif(), self.unif()) for _ in range(n)]

    def structured_pairs(self, n):
        fn = self.pair_float if self.isfloat else self.pair_int
        return [fn() for _ in range(n)]

    def boundary_pairs(self):
        B = self.B
        return [(a, b) for a in B for b in B]
