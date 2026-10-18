"""Float instructions in the differential test (docs/DESIGN.md section 8, "Float instructions").

A family declares a mnemonic float with fp="f32 scalar" (and so on; lifter/translate.py, FLOAT).
Everything else about testing it is here, so that no family carries any of it:

  * VARIANTS            the MXCSR settings every float instruction is run under;
  * float_operands      float-aware operand values per lane (boundary values, their pairings,
                        NaN pairs, rounding and denormal cases, random bits);
  * the NaN correction  the one place where the emulator is corrected for float code;
  * Hardware            the real CPU (tools/hworacle) as the expected side of the instruction test.

Measurements behind all of it: docs/findings/11-float-mode.md and docs/handoff/hw-oracle.md.
"""

import fcntl
import importlib.util
import os
import re
import struct
import sys
import tempfile
import time

from capstone import x86_const as X

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from lifter.translate import FLOAT, XMM, mnemonic_of     # noqa: E402

MXCSR_DEFAULT = 0x1F80
MXCSR_STATUS = 0x3F         # IE DE ZE OE UE PE: not modelled by lifted code, masked out of every comparison

# The MXCSR settings of the instruction test: the default, the three other rounding modes, DAZ,
# FTZ and both.  All exceptions stay masked.  A translation that ignores the mode fails under them.
VARIANTS = (("default", 0x1F80), ("round-down", 0x3F80), ("round-up", 0x5F80), ("round-zero", 0x7F80),
            ("daz", 0x1FC0), ("ftz", 0x9F80), ("daz+ftz", 0x9FC0))
VARIANT_NAMES = tuple(n for n, _ in VARIANTS)

LANE_BITS = {"f32": 32, "f64": 64}


def decl(ins):
    """(lane bits, number of lanes the instruction computes) if `ins` is declared float, else None."""
    d = FLOAT.get(mnemonic_of(ins))
    if d is None:
        return None
    w = LANE_BITS[d[0]]
    return w, (128 // w if d[1] else 1)


# ---- values ------------------------------------------------------------------------------------------

def _fmt(w):
    return (23, 8) if w == 32 else (52, 11)          # mantissa bits, exponent bits


def is_nan(x, w):
    mb, eb = _fmt(w)
    return (x >> mb) & ((1 << eb) - 1) == (1 << eb) - 1 and x & ((1 << mb) - 1) != 0


def quieted(x, w):
    return x | 1 << (_fmt(w)[0] - 1)


def _bits(v, w):
    return struct.unpack("<I", struct.pack("<f", v))[0] if w == 32 else struct.unpack("<Q", struct.pack("<d", v))[0]


_BOUNDARY = {}


def boundary(w):
    """The boundary bit patterns of binary32 or binary64, sorted: the float sets of
    experiments/afp/afp_inputs.py (162 and 197 values)."""
    if w in _BOUNDARY:
        return _BOUNDARY[w]
    mb, eb = _fmt(w)
    bias = (1 << (eb - 1)) - 1
    sign = 1 << (w - 1)
    inf = ((1 << eb) - 1) << mb
    q = 1 << (mb - 1)
    S = set()
    for s in (0, sign):
        S.add(s)                                                        # both zeros
        S.add(s | inf)                                                  # both infinities
        for p in (0, 1, 0x12345, 0x155555, 0x2aaaaa, q - 1):           # quiet NaNs, assorted payloads
            S.add(s | inf | q | (p & (q - 1)))
        for p in (1, 2, 0x12345, 0x155555, 0x2aaaaa, q >> 1, q - 1):   # signalling NaNs
            S.add(s | inf | (p & (q - 1)))
        for m in (1, 2, 3, 0xfff, q >> 1, q - 1, q, q + 1, (1 << mb) - 2, (1 << mb) - 1):      # denormals
            S.add(s | m)
        for b in (1 << mb, (1 << mb) + 1, (1 << mb) + 2, (2 << mb) - 1, 2 << mb,                # smallest normals
                  inf - 1, inf - 2, inf - (1 << mb), inf - (1 << mb) - 1):                      # largest normals
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
                   2147483649.0, 2147483648.9999998, 1e-310, 2.2250738585072014e-308, 1.7976931348623157e+308,
                   3.4028234663852886e+38, 3.4028235677973362e+38, 3.4028235677973366e+38, 3.5e38,
                   1.1754943508222875e-38, 1.1754942106924411e-38, 1.401298464324817e-45,
                   7.006492321624085e-46, 7.006492321624087e-46, 2.8e-45, 1e-46]):
            S.add(_bits(v, w) | s)                                      # ties, near-integers, narrowing edges
        for e in (31, 32, 63, 64):                                      # 2^31, 2^32, 2^63, 2^64 and neighbours
            c = (e + bias) << mb
            for d in (-2, -1, 0, 1, 2):
                S.add(s | (c + d))
    if w == 32:
        for e in (31, 63):
            c = (e + bias) << mb
            for d in (-1, 0, 1):
                S.add(sign | (c + d))
    else:
        for v in (-2147483648.0, -2147483648.5, -2147483649.0, -2147483649.5, -2147483647.5, -9223372036854775808.0):
            S.add(_bits(v, w))
        c = (63 + bias) << mb
        for d in (-3, -2, -1, 0, 1, 2, 3):
            S.add(sign | (c + d))
    _BOUNDARY[w] = sorted(S)
    return _BOUNDARY[w]


CATEGORIES = ("boundary x boundary", "two NaNs", "boundary x random", "random x boundary", "random x random",
              "close exponents (the sum must be rounded)", "denormals and the smallest normals",
              "cancellation (tiny or zero results)")


def lane_pair(rng, w, cat):
    """One pair of lane values (first operand, second operand) of category `cat` (CATEGORIES)."""
    B = boundary(w)
    mb, eb = _fmt(w)
    emax = (1 << eb) - 1
    sign = 1 << (w - 1)

    def nan():
        payload = rng.getrandbits(mb - 1) or 1
        return (sign if rng.random() < 0.5 else 0) | emax << mb | (1 << (mb - 1) if rng.random() < 0.5 else 0) | payload

    def finite(e):
        return (sign if rng.random() < 0.5 else 0) | e << mb | rng.getrandbits(mb)

    if cat == 0:
        return rng.choice(B), rng.choice(B)
    if cat == 1:
        return nan(), nan()
    if cat == 2:
        return rng.choice(B), rng.getrandbits(w)
    if cat == 3:
        return rng.getrandbits(w), rng.choice(B)
    if cat == 4:
        return rng.getrandbits(w), rng.getrandbits(w)
    if cat == 5:
        e = rng.randrange(1, emax)
        return finite(e), finite(min(emax - 1, max(1, e + rng.randrange(-(mb + 3), mb + 4))))
    if cat == 6:
        return finite(rng.choice((0, 0, 1, 2))), finite(rng.choice((0, 0, 1, 2, 3)))
    a = finite(rng.choice((1, 1, 2, 3, rng.randrange(1, emax))))
    return a, (a + rng.randrange(-3, 4)) ^ sign


def float_operands(ins, d, regs, xmm, rng, k, ea_of, peek):
    """Float-aware values for the operands of a float instruction: (xmm, pokes).

    `d` is decl(ins).  The first two xmm-or-memory operands get, in each lane the instruction
    computes, a pair of category (k + lane) mod 8, so the categories slide through the lanes of a
    packed instruction.  Lanes above those (the lanes a scalar instruction must preserve) keep
    the random bits they had in `xmm`.  A memory operand becomes a poke at its address, unless
    the address is not mapped (peek(addr, n) is None): that case faults on both sides."""
    w, nl = d
    xmm = bytearray(xmm)
    pokes = []
    pairs = [lane_pair(rng, w, (k + j) % len(CATEGORIES)) for j in range(nl)]
    ops = [op for op in ins.operands if op.type == X.X86_OP_MEM or (op.type == X.X86_OP_REG and op.reg in XMM)]
    for i, op in enumerate(ops[:2]):
        data = b"".join(p[i].to_bytes(w // 8, "little") for p in pairs)
        if op.type == X.X86_OP_REG:
            n = XMM[op.reg]
            xmm[16 * n:16 * n + len(data)] = data
        elif peek(ea_of(ins, op, regs), op.size) is not None:
            pokes.append((ea_of(ins, op, regs), data[:op.size]))
    return bytes(xmm), pokes


# ---- the NaN correction of the emulator --------------------------------------------------------------
#
# With two NaN operands (two quiet, two signalling, or one of each) Unicorn 2.1.4 (QEMU 5.0
# softfloat) applies the x87 rule: a quiet NaN wins over a signalling one, else the larger
# significand.  SSE does not: the result is the first source operand, quieted (Intel SDM vol. 1
# table 4-7; measured on a Core i5-7600K, docs/handoff/hw-oracle.md: 5,895 of 5,895 cases).
# Example: addss(7f800001, 7fe27b08) is 7fc00001 on the CPU and 7fe27b08 in Unicorn.
#
# Narrow on purpose: only these mnemonics (the two-operand arithmetic the table is about), only
# when their family declared them float, only the lanes in which both operands are NaNs.  With
# one NaN operand, or none, the emulator is right and nothing is touched.

NAN_FIRST_OPERAND = frozenset(op + sfx for op in ("add", "sub", "mul", "div") for sfx in ("ss", "sd", "ps", "pd"))


def nan_spec(ins):
    """None, or float_spec(ins) for an instruction the NaN correction applies to."""
    return float_spec(ins) if mnemonic_of(ins) in NAN_FIRST_OPERAND else None


def float_spec(ins):
    """None, or (destination xmm number, source, lane bits, lanes) for an instruction declared
    float of the form `op xmm, xmm/mem`.  source: ('x', xmm number) or ('m', the memory operand)."""
    d = decl(ins)
    ops = ins.operands
    if d is None or len(ops) != 2:
        return None
    if ops[0].type != X.X86_OP_REG or ops[0].reg not in XMM:
        return None
    if ops[1].type == X.X86_OP_REG and ops[1].reg in XMM:
        src = ("x", XMM[ops[1].reg])
    elif ops[1].type == X.X86_OP_MEM:
        src = ("m", ops[1])
    else:
        return None
    return XMM[ops[0].reg], src, d[0], d[1]


def nan_wants(a, b, w, nl):
    """[(lane, value)]: the lanes in which both operands (bytes a, b) are NaNs, with the result
    SSE gives there."""
    out = []
    wb = w // 8
    for j in range(nl):
        x = int.from_bytes(a[j * wb:(j + 1) * wb], "little")
        y = int.from_bytes(b[j * wb:(j + 1) * wb], "little")
        if is_nan(x, w) and is_nan(y, w):
            out.append((j, quieted(x, w)))
    return out


# ---- the real CPU as the expected side ---------------------------------------------------------------

_CLIENT = None


def client():
    """tools/hworacle/client.py, loaded under a name of its own."""
    global _CLIENT
    if _CLIENT is None:
        spec = importlib.util.spec_from_file_location("hworacle_client", os.path.join(REPO, "tools", "hworacle", "client.py"))
        _CLIENT = importlib.util.module_from_spec(spec)
        sys.modules["hworacle_client"] = _CLIENT
        spec.loader.exec_module(_CLIENT)
    return _CLIENT


_LOCK = os.path.join(tempfile.gettempdir(), "tf2-hworacle.lock")      # one batch at a time on the x86 machine
_DOWN = os.path.join(tempfile.gettempdir(), "tf2-hworacle.down")      # it did not answer; do not ask again for a while
DOWN_SECONDS = 600


class Hardware:
    """Expected results of float instruction cases from the real CPU.

    Cached answers first (inventory/hworacle_cache/insn-<module>/), the machine for the misses.
    HWORACLE=offline in the environment never opens ssh.  If the machine does not answer, the
    cached answers are used and the rest are reported as having none; nothing is retried for
    DOWN_SECONDS."""

    def __init__(self, D, session):
        self.D = D
        self.s = session
        self.C = client()
        self.family = "insn-" + session.mod
        self.offline = os.environ.get("HWORACLE", "auto") == "offline"
        self.unreachable = None
        self.sent = self.from_cache = 0
        self._mem = {}
        self._regions = {}

    def _mem_regions(self, mem_id):
        if mem_id not in self._mem:
            D, C = self.D, self.C
            mem = D.make_mem(*self.s.mem_kinds[mem_id])
            self._mem[mem_id] = (C.Region(D.ARENA_BASE, C.Blob(mem[:D.ARENA_SIZE]), C.PROT_R | C.PROT_W),
                                 C.Region(D.STACK_BASE, C.Blob(mem[D.ARENA_SIZE:]), C.PROT_R | C.PROT_W))
        return self._mem[mem_id]

    def regions(self, c, ins):
        """The memory of a case: the arena and stack of its memory image, and the 4 KiB pages of
        the image that the instruction and a RIP-relative operand lie in (not the whole image:
        a one-instruction case can touch nothing else of it, and the pages are small enough to
        travel inside the batch).  Mapped as the emulator maps the image: read, write, execute."""
        key = (c.mem_id, c.key)
        if key not in self._regions:
            D, C = self.D, self.C
            spans = [(ins.address, ins.address + ins.size)]
            for op in ins.operands:
                if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                    a = D.ea_of(ins, op, c.regs) - D.IMAGE_BASE
                    spans.append((a, a + op.size))
            pages = sorted({p for a, b in spans for p in range(a >> 12, ((b - 1) >> 12) + 1)})
            image = []
            for p in pages:
                if image and image[-1][1] == p:
                    image[-1][1] = p + 1
                else:
                    image.append([p, p + 1])
            self._regions[key] = tuple(
                C.Region(D.IMAGE_BASE + (a << 12), C.Blob(self.s.image_bytes[a << 12:b << 12]), C.PROT_R | C.PROT_W | C.PROT_X)
                for a, b in image) + self._mem_regions(c.mem_id)
        return self._regions[key]

    def ask(self, hw_cases):
        C = self.C
        res = C.Oracle(family=self.family, offline=True).run(hw_cases)
        missing = [i for i, r in enumerate(res) if r.outcome == C.NO_ANSWER]
        self.from_cache += len(res) - len(missing)
        if not missing or self.offline:
            return res
        try:
            if time.time() - os.path.getmtime(_DOWN) < DOWN_SECONDS:
                self.unreachable = "not asked: it did not answer less than %d s ago (%s)" % (DOWN_SECONDS, _DOWN)
                return res
        except OSError:
            pass
        with open(_LOCK, "w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            try:
                got = C.Oracle(family=self.family).run([hw_cases[i] for i in missing])
            except C.OracleUnavailable as e:
                self.unreachable = str(e).strip().split("\n")[-1][:200]
                with open(_DOWN, "w") as f:
                    f.write(self.unreachable + "\n")
                return res
        for i, r in zip(missing, got):
            res[i] = r
        self.sent += len(missing)
        return res

    def expect(self, cases, insns, ora):
        """Replace the emulator's result in `ora` by the hardware's for every float case, in
        place.  Returns {case id: (source, emulator differed)} with source one of 'hardware',
        'emulator only' (default MXCSR, no hardware answer: judged by the corrected emulator),
        'no hardware answer' (any other MXCSR: not judged) and 'hardware fault'.

        The mnemonics of oraclefix.HARDWARE_ONLY (rcp, rsqrt) are never 'emulator only': the
        emulator computes another function, so without a hardware answer the case is not judged,
        whatever its MXCSR."""
        D, C = self.D, self.C
        hw_cases = [C.Case(self.regions(c, insns[c.key]), D.IMAGE_BASE + c.func_rva, gpr=c.regs, rflags=c.eflags,
                           xmm=c.xmm, mxcsr=c.mxcsr, gs_base=c.gs_base, pokes=[(a, bytes(d)) for a, d in c.pokes],
                           tag=c.id) for c in cases]
        how = {}
        for c, hc, r in zip(cases, hw_cases, self.ask(hw_cases)):
            emu = ora[c.id]
            if r.outcome == C.DONE and r.rip == D.IMAGE_BASE + c.end:
                pages = {}
                for a, d in r.mem:
                    for p in range(a >> 12, ((a + len(d) - 1) >> 12) + 1):
                        pages[p << 12] = C.final_mem(hc, r, p << 12, D.PAGE)
                exp = {"status": "ok", "regs": list(r.gpr), "eflags": r.rflags & D.FLAG_MASK, "xmm": bytes(r.xmm),
                       "mxcsr": r.mxcsr, "calls": [], "pages": pages, "nan_fixed": emu.get("nan_fixed", 0),
                       "fixes": emu.get("fixes", {})}
                differs = emu["status"] != "ok" or any(emu[f] != exp[f] for f in ("regs", "eflags", "xmm", "pages")) \
                    or (emu["mxcsr"] ^ exp["mxcsr"]) & ~MXCSR_STATUS
                ora[c.id] = exp
                how[c.id] = ("hardware", bool(differs))
            elif r.outcome == C.FAULT and r.trapno == 13 and r.rip == D.IMAGE_BASE + c.insn_rva and not r.mem \
                    and D.place_aligned_operand(insns[c.key], list(c.regs), None):
                # #GP of a misaligned 16-byte operand: a defined trap, like the emulator's (oraclefix.py)
                exp = {"status": "trap", "trap_rva": c.insn_rva, "trap_what": D.OF.ALIGN_WHAT, "regs": list(r.gpr),
                       "eflags": r.rflags & D.FLAG_MASK, "xmm": bytes(r.xmm), "mxcsr": r.mxcsr, "calls": [], "pages": {},
                       "nan_fixed": 0, "fixes": emu.get("fixes", {})}
                differs = emu["status"] != "trap" or any(emu[f] != exp[f] for f in ("regs", "eflags", "xmm", "pages")) \
                    or (emu["mxcsr"] ^ exp["mxcsr"]) & ~MXCSR_STATUS
                ora[c.id] = exp
                how[c.id] = ("hardware", bool(differs))
            elif r.outcome in (C.DONE, C.FAULT):
                m = re.search(r"\((#\w+)", r.describe())         # the exception, without addresses
                ora[c.id] = {"status": "inconclusive", "reason": "hardware fault: %s" % (m.group(1) if m else "?"),
                             "detail": r.describe()}
                how[c.id] = ("hardware fault", False)
            elif c.mxcsr == MXCSR_DEFAULT and mnemonic_of(insns[c.key]) not in D.OF.HARDWARE_ONLY:
                how[c.id] = ("emulator only", False)
            else:
                ora[c.id] = {"status": "inconclusive", "reason": "no hardware answer"}
                how[c.id] = ("no hardware answer", False)
        return how
