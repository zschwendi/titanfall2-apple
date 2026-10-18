"""Guest state beyond registers, arithmetic flags and XMM: what the harness assumes about it,
and the instructions that need inputs or an expected side of their own.

Three things live here, each used by difftest.py in one place and listed in docs/DESIGN.md
section 8:

  * RFLAGS outside the seven flags of Ctx.  A Windows process runs in user mode with IF = 1,
    and lifted code cannot hold TF, AC, NT or ID.  The emulator starts with IF = 0 and runs
    as ring 0: START_RFLAGS is what every oracle run starts with, POPF_TRAP are the bits that
    make `popfq` a trap, popfq_patch undoes what a ring-0 popfq does and a user-mode one cannot.
  * The x87 control word: FPCW_DEFAULT on both sides at the start of every case (the emulator
    itself starts with 0), compared after it.
  * pcmpistri: string-like operands (random bytes hardly ever match), and the emulator's
    "equal ordered" aggregation, which is wrong (pcmpistri_wants).
"""

import os
import sys

from capstone import x86_const as X

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from lifter.translate import XMM, mnemonic_of     # noqa: E402

# ---- RFLAGS -------------------------------------------------------------------------------------------
#
# Intel SDM vol. 1, section 3.4.3 and the PUSHF/POPF entries.  In user mode (CPL 3, IOPL 0, as
# in every Windows process) IF reads as 1 and POPF changes neither IF nor IOPL; it does load TF,
# AC, NT and ID.  The real CPU's pushfq, asked through tools/hworacle, stores IF = 1 and TF = 0
# (its README, "What a case does not contain").  Lifted code: pushfq stores CF PF AF ZF SF DF OF
# from Ctx, bit 1, IF, and zero elsewhere; popfq loads the seven flags and traps if the value
# sets TF, AC, NT or ID, which it could not honour (lifter/insns/int_pushf.py).

RFLAGS_IF, RFLAGS_IOPL = 0x200, 0x3000
POPF_TRAP = 0x100 | 0x4000 | 0x40000 | 0x200000            # TF NT AC ID
POPF_TRAP_WHAT = "popfq sets TF, NT, AC or ID (lifted code cannot hold them)"
START_RFLAGS = RFLAGS_IF                                   # OR-ed into the oracle's start EFLAGS


def popfq_patch():
    """(and-mask, or-value) for EFLAGS after the emulator executed a popfq: IF and IOPL as a
    user-mode popfq leaves them."""
    return ~RFLAGS_IOPL, RFLAGS_IF


def popfq_value(rng):
    """The 8 bytes a popfq of the instruction test pops: any bits, but in three states of four
    none of POPF_TRAP (random memory would be a trap in fifteen states of sixteen)."""
    v = rng.getrandbits(64)
    if rng.random() < 0.75:
        v &= ~POPF_TRAP
    return v.to_bytes(8, "little")


# ---- x87 control word ---------------------------------------------------------------------------------
#
# Ctx.fpcw is all the x87 state lifted code has (docs/findings/15-x87.md: the game's real code
# only stores, loads and clears).  0x027F is what a Windows x64 thread starts with (all
# exceptions masked, 53-bit precision, round to nearest); Unicorn 2.1.4 reports 0 at start.

FPCW_DEFAULT = 0x027F


def fpcw_start(ins, rng):
    """The control word an instruction-test case starts with: the default, but any 16 bits in
    three states of four of an fnstcw, which would otherwise store the same value every time."""
    if mnemonic_of(ins) == "fnstcw" and rng.random() < 0.75:
        return rng.getrandbits(16)
    return FPCW_DEFAULT

# ---- pcmpistri ----------------------------------------------------------------------------------------
#
# Intel SDM vol. 2, section 4.1 and the PCMPISTRI entry.  The emulator (QEMU 5.0) computes the
# "equal ordered" aggregation (imm8[3:2] = 11, a substring search) only at positions where the
# whole of xmm1's string fits before the end of xmm2/m128's; the SDM compares up to the end of
# the 16 bytes and reports a match that runs off the end (IntRes1[j] is the AND over
# i = 0 .. n-1-j only).  That partial match is what a strstr loop continues from.  Measured on
# a Core i5-7600K (2026-10-09, cache family fo-pcmpistri, control bytes 0x0c 0x0d 0x15 0x40
# 0x41, 600 cases each): the CPU agrees with pcmpistri_model in 3,000 of 3,000; the emulator
# differs from the CPU in 36 (0x0c) and 45 (0x0d), always in ECX and CF together, never in
# the three other control bytes.  So for equal ordered, ECX, CF and OF, the three results
# derived from IntRes2, are taken from the model; the emulator keeps everything else.

_ARITH = {"cf": 0, "pf": 2, "af": 4, "zf": 6, "sf": 7, "of": 11}


def pcmpistri_model(a, b, ctl):
    """(ECX, EFLAGS bits) of pcmpistri xmm1 = a, xmm2/m128 = b (16 bytes each), imm8 = ctl:
    the SDM's four steps, for every control byte."""
    w = ctl & 1
    n, bits = (8, 16) if w else (16, 8)
    ea = [int.from_bytes(a[k * bits // 8:(k + 1) * bits // 8], "little") for k in range(n)]
    eb = [int.from_bytes(b[k * bits // 8:(k + 1) * bits // 8], "little") for k in range(n)]
    la = next((k for k in range(n) if ea[k] == 0), n)
    lb = next((k for k in range(n) if eb[k] == 0), n)
    if ctl & 2:                                            # signed elements (matters for ranges only)
        ea = [v - (1 << bits) if v >> (bits - 1) else v for v in ea]
        eb = [v - (1 << bits) if v >> (bits - 1) else v for v in eb]
    agg = (ctl >> 2) & 3
    r1 = 0
    for j in range(n):
        if agg == 0:
            bit = j < lb and any(eb[j] == ea[i] for i in range(la))
        elif agg == 1:
            bit = j < lb and any(ea[i] <= eb[j] <= ea[i + 1] for i in range(0, la - 1, 2))
        elif agg == 2:
            bit = (j >= la and j >= lb) or (j < la and j < lb and ea[j] == eb[j])
        else:
            bit = all(i >= la or (j + i < lb and ea[i] == eb[j + i]) for i in range(n - j))
        r1 |= int(bit) << j
    pol = (ctl >> 4) & 3
    r2 = r1 ^ ((1 << n) - 1) if pol == 1 else r1 ^ ((1 << lb) - 1) if pol == 3 else r1
    idx = n if r2 == 0 else r2.bit_length() - 1 if ctl & 0x40 else (r2 & -r2).bit_length() - 1
    return idx, (1 if r2 else 0) | ((lb < n) << 6) | ((la < n) << 7) | ((r2 & 1) << 11)


def pcmpistri_plan(ins):
    """(xmm1 number, operand 2, imm8) for a pcmpistri the equal-ordered compensation applies
    to, else None."""
    if mnemonic_of(ins) != "pcmpistri" or len(ins.operands) != 3:
        return None
    ctl = ins.operands[2].imm & 0xFF
    if (ctl >> 2) & 3 != 3 or ins.operands[0].reg not in XMM:
        return None
    return XMM[ins.operands[0].reg], ins.operands[1], ctl


def pcmpistri_wants(a, b, ctl):
    """[(register, and-mask, or-value)] with register "rcx" or "eflags": what the CPU leaves
    in ECX, CF and OF for an equal-ordered pcmpistri."""
    ecx, fl = pcmpistri_model(a, b, ctl)
    keep = (1 << _ARITH["cf"]) | (1 << _ARITH["of"])
    return [("rcx", 0, ecx), ("eflags", ~keep, fl & keep)]


_ALPHABETS = ((1, 2), (0x41, 0x42, 0x43), (1, 0x7F, 0x80, 0xFF), tuple(range(1, 9)))


def _stringy(rng, w):
    """16 bytes that look like a string of bytes (w = 0) or words: a small alphabet, and in
    three of five a terminator, after which anything follows."""
    n, size = (8, 2) if w else (16, 1)
    alpha = list(rng.choice(_ALPHABETS))
    if w and rng.random() < 0.5:
        alpha = [v | (rng.choice((0, 1, 0x80)) << 8) for v in alpha]
    e = [rng.choice(alpha) for _ in range(n)]
    if rng.random() < 0.6:
        e[rng.randrange(n)] = 0
    return b"".join(v.to_bytes(size, "little") for v in e)


def pcmpistri_operands(rng, ctl):
    """(a, b), 16 bytes each, for one pcmpistri case: in three cases of four string-like, with
    b often holding a prefix of a at some position (also running off the end) or equal to a;
    in the fourth random bytes."""
    w = ctl & 1
    if rng.random() < 0.25:
        return rng.randbytes(16), rng.randbytes(16)
    a, b = _stringy(rng, w), _stringy(rng, w)
    r = rng.random()
    if r < 0.15:
        b = a
    elif r < 0.5:
        n, size = (8, 2) if w else (16, 1)
        j, ln = rng.randrange(n), rng.randrange(1, n + 1)
        bb = bytearray(b)
        bb[j * size:(j + ln) * size] = a[:ln * size][:(n - j) * size]
        b = bytes(bb)
    return a, b


def pcmpistri_state(ins, regs, xmm, rng, ea_of):
    """The xmm bytes and pokes of an instruction-test state of a pcmpistri: its two operands
    replaced by pcmpistri_operands (a register used twice keeps one value)."""
    a, b = pcmpistri_operands(rng, ins.operands[2].imm & 0xFF)
    xmm, pokes = bytearray(xmm), []
    src = ins.operands[1]
    if src.type == X.X86_OP_MEM:
        pokes.append((ea_of(ins, src, regs), b))
    elif src.reg in XMM:
        xmm[16 * XMM[src.reg]:16 * XMM[src.reg] + 16] = b
    if ins.operands[0].reg in XMM:
        xmm[16 * XMM[ins.operands[0].reg]:16 * XMM[ins.operands[0].reg] + 16] = a
    return bytes(xmm), pokes
