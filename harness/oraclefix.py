"""Oracle compensations: where the emulator is known to be wrong and what this harness does about it.

Unicorn 2.1.4 (QEMU 5.0) is the expected side of the function test and of every instruction case
without a hardware answer.  It is wrong in a few known, narrow ways.  Each is corrected here,
once, by a rule taken from the Intel manual and measured on a real CPU (tools/hworacle); each
is listed in docs/DESIGN.md section 8 with its justification, counted in both tests' summaries,
and pinned by self-tests (a case it must change, a neighbouring case it must not).

  * alignment     a legacy SSE instruction with a misaligned 16-byte memory operand raises #GP;
  * the value corrections of float instructions (plan, wants):
      nan         which operand a two-NaN operation returns (the rule itself is in floatcase.py);
      minmax      min and max under DAZ return a zero where the operand selected is a denormal;
      ftz         MXCSR.FTZ, which the emulator ignores: a tiny result is a signed zero.
  * m64           cvtdq2pd and cvtps2pd read 8 bytes of memory; the emulator reads 16 and faults
                  when the second 8 are not mapped.  It is let read zeros there (M64_READ_AS_16);
  * hardware only rcpss, rcpps, rsqrtss, rsqrtps: the emulator returns the exact quotient, the CPU
                  a table value.  Not corrected: a case that executes one is judged by the real
                  CPU or not at all (HARDWARE_ONLY).

The emulator hooks that apply them are Oracle._info / _code / _fp_apply in difftest.py.
"""

import os
import sys
from fractions import Fraction

from capstone import x86_const as X

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from lifter.translate import mnemonic_of     # noqa: E402
import floatcase as FC                       # noqa: E402

# ---- alignment of a 16-byte memory operand ------------------------------------------------------------
#
# Intel SDM vol. 2, section 2.4 (exception classes of SSE instructions, types 1, 2 and 4):
# "#GP(0): legacy SSE: memory operand is not 16-byte aligned", with the footnote that LDDQU,
# MOVUPD, MOVUPS, PCMPESTRI, PCMPESTRM, PCMPISTRI and PCMPISTRM do not; MOVDQU says the same on
# its own page.  Unicorn checks no alignment at all.  Measured on a Core i5-7600K (2026-10-09,
# cache family fo-align): addps, addpd, subps, sqrtps, minps, rcpps, cmpps, cvtdq2ps, pshufd and
# a movaps store fault with #GP at offsets 1, 4 and 8; the fault comes before the page fault of
# an unmapped operand and before anything is written; movups, movdqu, and the 4- and 8-byte
# operands of addss, comiss and cvtps2pd do not fault.  1,056 of 1,056 cases of the earlier
# cross-check agree (docs/handoff/hw-oracle.md).
#
# The lifter has the same rule in its own words (lifter/translate.py: UNALIGNED_M128, xalign).

UNALIGNED_OK = frozenset(("movups", "movupd", "movdqu", "lddqu", "pcmpestri", "pcmpestrm", "pcmpistri", "pcmpistrm"))
# Capstone 5.0.7 calls comisd's m64 operand 16 bytes; it is 8 and has no alignment rule (the
# lifter's copy: translate.DECODER_M64).
DECODER_M64 = frozenset(("comisd",))
ALIGN_WHAT = "#GP (16-byte memory operand of an SSE instruction not 16-byte aligned)"
_LEGACY = frozenset((0xF0, 0xF2, 0xF3, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65, 0x66, 0x67))


def aligned_operand(ins):
    """The memory operand that `ins` requires to be 16-byte aligned, or None.

    Only the legacy SSE encodings (opcode maps 0F, 0F38, 0F3A; a VEX or EVEX form has no such
    rule), only a memory operand of exactly 16 bytes.  cmpxchg16b has a rule of its own, which
    the oracle already implements."""
    raw = bytes(ins.bytes)
    i = 0
    while i < len(raw) and raw[i] in _LEGACY:
        i += 1
    if i < len(raw) and raw[i] & 0xF0 == 0x40:           # REX
        i += 1
    if i >= len(raw) or raw[i] != 0x0F:                  # one-byte opcodes; C4, C5 (VEX), 62 (EVEX)
        return None
    name = mnemonic_of(ins)
    if name in UNALIGNED_OK or name in DECODER_M64 or name == "cmpxchg16b":
        return None
    for op in ins.operands:
        if op.type == X.X86_OP_MEM and op.size == 16:
            return op
    return None


# Where the instruction test puts such an operand.  Of every five consecutive states four are
# aligned (what a correct program does: the result is compared) and one is not (the fault is
# compared), at these offsets in turn.  RIP-relative and absolute operands lie where they lie.
MISALIGNED_EVERY = 5
MISALIGNED_AT = (8, 4, 1, 12, 15, 2)


def wanted_offset(k):
    """The offset modulo 16 that state k of an instruction gives its aligned operand."""
    return MISALIGNED_AT[(k // MISALIGNED_EVERY) % len(MISALIGNED_AT)] if k % MISALIGNED_EVERY == MISALIGNED_EVERY - 1 else 0


# ---- an 8-byte operand the emulator reads as 16 bytes ---------------------------------------------------
#
# Intel SDM, CVTDQ2PD and CVTPS2PD: "xmm1, xmm2/m64" - two 32-bit lanes are converted, the memory
# operand is 8 bytes.  Unicorn 2.1.4 loads 16 bytes for both: with the operand in the last 8 mapped
# bytes it reports UC_ERR_READ_UNMAPPED where the instruction completes (insn-wave review, finding
# 2: f3 0f e6 0b and 0f 5a 0b with rbx = end of the arena - 8; lifted code reads 8 bytes and
# returns the manual's value, as do comisd and cvtss2sd in the emulator at the same address).  The
# second 8 bytes never reach the result, so the oracle lets the emulator read zeros for them: the
# unmapped page that read runs onto is mapped for that one instruction (Oracle._bad_access) and
# taken away before the next.  Nothing else changes: a case whose operand is wholly mapped is not
# touched, and an operand whose own 8 bytes reach unmapped memory still faults.
# Not measured on the CPU with the next page unmapped (the hardware runner maps its own memory);
# measured there: the 8-byte operand of cvtps2pd takes no alignment fault (above), which a 16-byte
# operand would.

M64_READ_AS_16 = frozenset(("cvtdq2pd", "cvtps2pd"))


def overread_operand(ins):
    """The 8-byte memory operand of `ins` that the emulator reads as 16 bytes, or None."""
    if mnemonic_of(ins) not in M64_READ_AS_16:
        return None
    for op in ins.operands:
        if op.type == X.X86_OP_MEM and op.size == 8:
            return op
    return None


def overread_page(ea, addr):
    """The page to lend the emulator for an invalid read at `addr` while it loads 16 bytes for the
    8-byte operand at `ea`, or None if the read is not that one (it is the operand's own bytes
    that are missing, or another address altogether)."""
    page = (ea + 15) & ~0xFFF
    return page if ea + 8 <= addr < ea + 16 and (ea + 7) & ~0xFFF != page else None


# ---- float values: shared pieces ----------------------------------------------------------------------

MXCSR_DAZ, MXCSR_FTZ = 0x40, 0x8000
_FMT = {32: (24, -126, 127, 8), 64: (53, -1022, 1023, 11)}       # precision, emin, bias, exponent bits
_SUFFIXES = ("ss", "sd", "ps", "pd")


def is_denormal(x, w):
    p, _, _, eb = _FMT[w]
    return (x >> (p - 1)) & ((1 << eb) - 1) == 0 and x & ((1 << (p - 1)) - 1) != 0


def _daz(x, w):
    """What a denormal operand is under DAZ: a zero of its sign."""
    return x & (1 << (w - 1)) if is_denormal(x, w) else x


def _value(x, w):
    """The value of a finite operand as a Fraction; None for an infinity or a NaN."""
    p, emin, bias, eb = _FMT[w]
    e, fr = (x >> (p - 1)) & ((1 << eb) - 1), x & ((1 << (p - 1)) - 1)
    if e == (1 << eb) - 1:
        return None
    v = Fraction(fr) * Fraction(2) ** (emin - (p - 1)) if e == 0 else Fraction(fr + (1 << (p - 1))) * Fraction(2) ** (e - bias - (p - 1))
    return -v if x >> (w - 1) else v


def _lanes(a, w, nl):
    return [int.from_bytes(a[j * w // 8:(j + 1) * w // 8], "little") for j in range(nl)]


# ---- min and max under DAZ ----------------------------------------------------------------------------
#
# SDM, MINSS (MIN(SRC1, SRC2)): both zero -> SRC2; either a NaN -> SRC2; SRC1 < SRC2 -> SRC1; else
# SRC2 (max: SRC1 > SRC2).  With MXCSR.DAZ set a denormal operand is a zero of its sign, for the
# comparison and for what is returned: the CPU returns the selected operand flushed.  Unicorn
# returns its original bits.  Measured on a Core i5-7600K (2026-10-09, cache family fo-minmax):
# this rule gives the CPU's result in 12,400 of 12,400 lanes (min and max, ss sd ps pd, 22 x 22
# boundary values and 60 denormal pairs per width; 6,288 lanes under DAZ, 4,112 without), and so
# does the host's fmin/fmax in the guest float mode, which is the translation (runtime.h,
# tf_fmin32..tf_fmax64).  The compare-select form `a < b ? a : b` differs in 2,064 of the 6,288
# DAZ lanes.  Earlier: 822 of 822 cross-check cases fit the rule (docs/handoff/hw-oracle.md).
#
# Applied only with DAZ set, only to lanes in which an operand is a denormal.

MINMAX = frozenset(op + sfx for op in ("min", "max") for sfx in _SUFFIXES)


def minmax_wants(name, a, b, w, nl):
    """[(lane, value)]: the lanes with a denormal operand, and what the CPU returns there under DAZ."""
    out = []
    for j, (x, y) in enumerate(zip(_lanes(a, w, nl), _lanes(b, w, nl))):
        if not (is_denormal(x, w) or is_denormal(y, w)):
            continue
        if FC.is_nan(x, w) or FC.is_nan(y, w):
            sel = y
        else:
            fx, fy = _value(_daz(x, w), w), _value(_daz(y, w), w)
            inf = lambda v, bits: v if v is not None else (Fraction(-1) if bits >> (w - 1) else Fraction(1)) * Fraction(2) ** 2000
            fx, fy = inf(fx, x), inf(fy, y)
            sel = x if (fx > fy if name.startswith("max") else fx < fy) else y
        out.append((j, _daz(sel, w)))
    return out


# ---- FTZ ----------------------------------------------------------------------------------------------
#
# SDM vol. 1, 10.2.3.3: with MXCSR.FTZ set (and the underflow exception masked, which is all the
# harness runs) a tiny result is replaced by a zero with the sign of the true result.  Tiny is
# decided after rounding (4.9.1.5): the exact result, rounded to the destination precision as if
# the exponent range were unbounded, lies strictly between -2^emin and 2^emin.  A result that
# rounds up to 2^emin is not tiny and is not flushed.  Unicorn 2.1.4 ignores FTZ altogether.
# The rule is the one of experiments/afp (afp_lab.ftz_patch, afp_model.tiny_after_rounding),
# which matched a Core i5-7600K in 5,790 of 5,790 cases (docs/handoff/hw-oracle.md).
#
# Applied only with FTZ set, only to these mnemonics, only to lanes with two finite operands
# (after DAZ) whose exact result is tiny.

FTZ_ARITH = frozenset(op + sfx for op in ("add", "sub", "mul", "div") for sfx in _SUFFIXES)


def _tiny(v, w, rc):
    """Is the non-zero exact result v tiny after rounding (MXCSR.RC = rc) to lane width w?"""
    p, emin, _, _ = _FMT[w]
    a = abs(v)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if (a.numerator < a.denominator << e) if e >= 0 else (a.numerator << -e < a.denominator):
        e -= 1                                           # e = floor(log2(a))
    if e < emin - 1:
        return True
    if e >= emin:
        return False
    m = a / Fraction(2) ** (e - (p - 1))                 # in [2^(p-1), 2^p): does it round up to 2^p?
    fl = m.numerator // m.denominator
    rem = m - fl
    neg = v < 0
    if rem == 0:
        up = False
    elif rc == 0:
        up = rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl & 1)
    elif rc == 3:
        up = False
    else:
        up = (rc == 2) != neg                            # 2: toward +inf, 1: toward -inf
    return fl + up < 1 << p


def ftz_wants(name, a, b, w, nl, mxcsr):
    """[(lane, value)]: the lanes whose result is tiny, with the signed zero FTZ makes of it."""
    out = []
    op, rc, sign = name[:3], (mxcsr >> 13) & 3, 1 << (w - 1)
    for j, (x, y) in enumerate(zip(_lanes(a, w, nl), _lanes(b, w, nl))):
        if mxcsr & MXCSR_DAZ:
            x, y = _daz(x, w), _daz(y, w)
        fx, fy = _value(x, w), _value(y, w)
        if fx is None or fy is None or (op == "div" and fy == 0):
            continue
        v = fx + fy if op == "add" else fx - fy if op == "sub" else fx * fy if op == "mul" else fx / fy
        if v != 0 and _tiny(v, w, rc):
            out.append((j, sign if v < 0 else 0))
    return out


# ---- instructions only the real CPU can judge ---------------------------------------------------------
#
# rcpss, rcpps, rsqrtss and rsqrtps return a table approximation: 12 bits of result, relative
# error at most 1.5 * 2^-12 (SDM, RCPSS), and which 12 bits is the CPU vendor's choice.  Unicorn
# 2.1.4 returns the exact quotient, correctly rounded: rcpss(1.0) is 3f800000 there and 3f7ff000
# on a Core i5-7600K; over the sweep of harness/recip_sweep.py the exact quotient equals the
# CPU's result in 97 of 242,032 lanes with a normal operand.  So for these the emulator is not a
# reference at all, and nothing here tries to make it one: there is no value correction.
#   * The emulator stops before executing one, and the case is inconclusive with the reason
#     below (Oracle._code).  In the function test that is the end of it: a function is judged
#     only by its cases that do not reach such an instruction.
#   * In the instruction test the expected side is the real CPU (floatcase.Hardware.expect); a
#     case the CPU has not answered is inconclusive under every MXCSR setting, the default too
#     (for other float mnemonics the default setting falls back on the corrected emulator).
# A translation that returns the exact quotient therefore fails wherever it is judged
# (tests/run_selftests.py, test_recip_is_judged_by_the_cpu_only).  The #GP of a misaligned
# 16-byte operand does not depend on the value and stays the emulator's to report.

HARDWARE_ONLY = frozenset(("rcpss", "rcpps", "rsqrtss", "rsqrtps"))
HARDWARE_ONLY_WHAT = ("only the real CPU can judge rcp/rsqrt: a table approximation there, the exact quotient "
                      "in the emulator")


# ---- what the hooks do for one instruction ------------------------------------------------------------

class Plan:
    """What the oracle must do around one instruction: `align`, the operand whose misalignment
    is a #GP (or None); `value`, None or (destination xmm number, source, lane bits, lanes,
    mnemonic, the value corrections that apply to the mnemonic), source: ('x', xmm number) or
    ('m', the memory operand); `unjudged`, None or why the emulator's result of this instruction
    is no reference (HARDWARE_ONLY): the oracle stops there; `overread`, the 8-byte memory operand
    the emulator reads as 16 bytes (or None)."""
    __slots__ = ("align", "value", "unjudged", "overread")

    def __init__(self, align, value, unjudged=None, overread=None):
        self.align, self.value, self.unjudged, self.overread = align, value, unjudged, overread


def plan(ins):
    """None for an instruction no compensation applies to.  A value correction applies only to
    a mnemonic of its table that a family declared float (FC.float_spec)."""
    a = aligned_operand(ins)
    v = None
    name = mnemonic_of(ins)
    if name in HARDWARE_ONLY:
        return Plan(a, None, HARDWARE_ONLY_WHAT)
    spec = FC.float_spec(ins)
    if spec is not None:
        kinds = tuple(k for k, table in (("nan", FC.NAN_FIRST_OPERAND), ("minmax", MINMAX), ("ftz", FTZ_ARITH)) if name in table)
        if kinds:
            v = spec + (name, kinds)
    o = overread_operand(ins)
    return None if a is None and v is None and o is None else Plan(a, v, overread=o)


def wants(value, a, b, mxcsr):
    """[(lane, value, correction)] for the operands `a` (destination) and `b` (source), 16 bytes
    each, of an instruction about to run under `mxcsr`: what the CPU will leave in those lanes."""
    _, _, w, nl, name, kinds = value
    out = []
    if "nan" in kinds:
        out += [(j, v, "nan") for j, v in FC.nan_wants(a, b, w, nl)]
    if "minmax" in kinds and mxcsr & MXCSR_DAZ:
        out += [(j, v, "minmax") for j, v in minmax_wants(name, a, b, w, nl)]
    if "ftz" in kinds and mxcsr & MXCSR_FTZ:
        out += [(j, v, "ftz") for j, v in ftz_wants(name, a, b, w, nl, mxcsr)]
    return out


# ---- counting -----------------------------------------------------------------------------------------

# The compensations, by the key under which a run counts them (Oracle.run: res["fixes"]).
COMPENSATIONS = (("nan", "NaN operand choice"), ("align", "misaligned 16-byte operand (#GP)"),
                 ("minmax", "min/max under DAZ"), ("ftz", "FTZ"),
                 ("pcmpistri", "pcmpistri equal ordered"), ("popfq", "popfq IF/IOPL"),       # statefix.py
                 ("m64", "8-byte operand read as 16"),
                 ("bscan", "bsf/bsr r32 with a zero source"))                                   # difftest.BSCAN_RULE


def counts(fixes):
    """{description: number of cases whose emulator result the compensation changed}, all of them."""
    return {what: fixes.get(key, 0) for key, what in COMPENSATIONS}


def counts_line(fixes):
    return ", ".join("%s %d" % (what, fixes.get(key, 0)) for key, what in COMPENSATIONS)
