#!/usr/bin/env python3
"""Write briefs and unit files for small instruction tasks: one mini-family per Haiku worker.

The instruction counterpart of tools/swarm/make_shim_tasks.py, shaped for the same cost rule
(a Haiku request over 100,000 tokens costs five times as much): a task is a mini-family of one
to four mnemonics that are translated the same way, with its own three files
(lifter/insns/<name>.py, harness/insns/<name>.h, tests/vectors/<name>.py) and its own
`make family-check FAMILY=<name>`.  Two workers never touch the same file.  The brief carries
the encodings, the semantics, the helper signatures, a finished family to copy from and the
commands that print the test vectors; the reading list is lifter/insns/README.md and nothing
else.

    .venv/bin/python tools/swarm/make_insn_tasks.py              write briefs and unit files
    .venv/bin/python tools/swarm/make_insn_tasks.py --out DIR    ... under DIR instead of swarm/
    .venv/bin/python tools/swarm/make_insn_tasks.py --show NAME  print one brief
    .venv/bin/python tools/swarm/make_insn_tasks.py --list       name, group, state, mnemonics, game counts
    .venv/bin/python tools/swarm/make_insn_tasks.py --check      verify the table; exit 1 if it is wrong
    .venv/bin/python tools/swarm/make_insn_tasks.py --verify NAME   the family registers exactly its task's
                                                                 mnemonics (part of every unit's check)

Units are swarm/units/insn-<name>.json, kind "insn-task", model haiku; run them with
tools/swarm/wave.py --kind insn-task.  Re-running overwrites briefs and leaves existing unit
files (and their state) alone.  --base BRANCH adds BASE=BRANCH to the family-check commands
(for a worktree that was not cut from wip/lifter-v0).

States in the table:
  todo     a brief and a unit file are written
  done     the family is in the repository; nothing is written
  strong   needs a stronger model or a decision first (`why` says which); nothing is written
Float groups (FLOAT_GROUPS) are cut into mini-families here, but nothing is written for them
until FLOAT_PATTERN below is filled from docs/handoff/lifter-float-infra.md: their results
depend on rounding, NaN rules and MXCSR, and the pattern for that is that unit's to define.
"""

import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [ROOT, os.path.join(ROOT, "tools"), os.path.join(ROOT, "tools", "census")]

FLOAT_GROUPS = ("sse_arith_ss", "sse_arith_sd", "sse_arith_packed", "sse_comi", "sse_cmp_pred", "sse_cvt_fp",
                "sse_cvt_int", "sse_recip", "fp_control")

# Filled by whoever reads docs/handoff/lifter-float-infra.md: the text of the brief's section
# "How a float instruction is written" (how a float mnemonic is declared, which C formulations
# are allowed, which helpers of harness/fpmode.h to call, what the vectors must cover) and the
# family to copy from.  While FLOAT_PATTERN is None the float tasks are listed and checked but
# no brief or unit file is written for them.
def _float_pattern():
    """The brief's section "How a float instruction is written": the README's float section, how
    operands and results are reached, and the formulations docs/findings/11-float-mode.md measured
    exact (its results table, without the column for the mode lifted code does not run in)."""
    readme = open(os.path.join(ROOT, "lifter", "insns", "README.md")).read()
    a = readme.index("## Float mnemonics")
    section = readme[a:readme.index("\n## ", a + 5)].split("\n", 1)[1].strip()
    rows, inside = [], False
    for line in open(os.path.join(ROOT, "docs", "findings", "11-float-mode.md")):
        if line.startswith("| Operation |") and "Exact C" in line:
            inside = True
        elif inside and not line.startswith("|"):
            break
        elif inside and not line.startswith("|---"):
            cells = [c.strip().replace("\\|", "|") for c in re.split(r"(?<!\\)\|", line.strip().strip("|"))]
            if len(cells) == 4:
                rows.append("- %s. In the guest mode: %s. C: %s" % (cells[0], cells[2], cells[3]))
    if len(rows) < 10:
        sys.exit("make_insn_tasks: the results table of docs/findings/11-float-mode.md was not found")
    return FLOAT_TEXT % (section, "\n".join(rows))


FLOAT_TEXT = """%s

### Operands and results

- A float source: `tr.fval(i, 32)` or `tr.fval(i, 64)` is a C expression of type `float` or
  `double` for operand `i`, lane 0 of an xmm register or a memory operand of exactly that size.
  Copy it into a local variable first: the source is read before anything is written, because
  source and destination may be the same register.
- A packed source: `Xmm s = ...;` from `tr.xval(i)`, the whole xmm register or 16 bytes of
  memory, copied before anything is written; its lanes are `s.f[j]`, `s.pd[j]`, `s.d[j]`. For
  16 bytes of memory `tr.xval` also emits the fault x86 raises when the address is not 16-byte
  aligned, so never write `LDX(tr.ea(i))` yourself. Some packed instructions read less than 16
  bytes from memory (`cvtps2pd xmm, m64`, `cvtdq2pd xmm, m64`: `tr.ops[i].size` is 8): `tr.xval`
  refuses those; read the lanes with `LDF32(m)`, `LDF32(m + 4)` (floats) or `LD32(m)`,
  `LD32(m + 4)` (integers) where `m = tr.ea(i)`, and there is no alignment rule.
- An xmm destination: `d = tr.xr(0)` is its register number (`None` when operand 0 is not an xmm
  register: `raise Unsupported(...)`). Its lanes are `ctx->xmm[d].f[0..3]` (`float`),
  `.pd[0..1]` (`double`), `.d[0..3]` (`uint32_t`) and `.q[0..1]` (`uint64_t`). A scalar
  instruction writes lane 0 and leaves every other bit of the destination as it was.
- An integer operand (the source of `cvtsi2ss`, the destination of `cvttss2si`):
  `tr.val(i, bits)` is a C expression of type `uint<bits>_t` for a register or memory operand of
  that size (`tr.ops[i].size * 8` is the size), and `tr.wr(i, expr, bits)` is the C statement
  that stores into it (a 32-bit register write clears the upper half, as x86 does). Convert
  through the signed type, `(int32_t)` or `(int64_t)`.
- Flags (`comiss` and its relatives): `ctx->cf`, `ctx->pf`, `ctx->af`, `ctx->zf`, `ctx->sf`,
  `ctx->of`, each a `uint8_t` holding 0 or 1. Write all six.
- `fp=` names the lane type of the float operand the instruction reads (`cvtsd2ss` reads a
  double: `"f64 scalar"`); for `cvtsi2ss` and `cvtsi2sd`, which read an integer, the type they
  write.

### The formulations measured exact

From the results table of `docs/findings/11-float-mode.md`. Use the entry for your instruction
and nothing else. If your instruction has no entry, or its entry says it is not exact in the
guest mode, stop and report: do not invent a formulation.

%s

### The check asks a real processor

`make family-check` takes the expected result of every float case from a real Intel CPU on
another machine, under seven MXCSR settings; the first run of a family takes several minutes
longer than a later one. If it says the machine could not be reached, run it once more after
ten minutes; if it says so again, stop and report. If one setting (for example `daz`)
disagrees with the CPU while the others pass, do not bend the C until it fits: report it. The
formulation is then not exact, and that is not yours to settle.
"""

# Float tasks handed out so far: the instructions whose formulation the finding measured exact
# and whose operand shapes the section above covers.  The packed ones were released when
# lifter-float-oracle settled the 16-byte memory operand (README rule 3) and wrote their pattern
# family, sse_addsub_p; packed min and max when it settled their form under DAZ (tf_fmin/tf_fmax;
# the scalar family sse_minmax_s is its own).  Still not handed out: fp_x87cw, marked
# for a stronger model (the other one, sse_recip, is done: unit lifter-recip).
FLOAT_RELEASED = ("sse_mul_s", "sse_sub_s", "sse_div_s", "sse_sqrt_s", "sse_comi", "sse_cmpss", "sse_cmpsd",
                  "sse_cvt_sd_ss", "sse_cvtsi2s", "sse_cvtt_s2si", "sse_cvt_s2si",
                  "sse_muldiv_p", "sse_sqrt_p", "sse_hadd", "sse_cmpps", "sse_cmppd",
                  "sse_cvtdq2p", "sse_cvt_pd_ps", "sse_cvtp2dq", "sse_minmax_p")
FLOAT_PATTERN = _float_pattern()
FLOAT_MODEL = "sse_scalar_arith"
FLOAT_PACKED_MODEL = "sse_addsub_p"         # the family a task copies from when its mnemonics compute every lane

# The family every vector task copies from: the proof run of this unit (docs/handoff/insn-tasks.md).
VECTOR_MODEL = "sse_shift_dq"

# Mnemonics of the work list that are nobody's task here, and why.
ELSEWHERE = {m: "lifter-float-infra translates it (the pattern family sse_scalar_arith)" for m in ("addss", "addsd")}
ELSEWHERE.update({m: "lifter-float-infra translates it (MXCSR is part of the float infrastructure)"
                  for m in ("stmxcsr", "ldmxcsr")})
GATED = {
    "gated_crypto": "aesenc aesdec sha256rnds2 aesenclast sha1rnds4 sha1nexte sha1msg1 sha1msg2 pclmulqdq sha256msg1 "
                    "sha256msg2 aesdeclast aeskeygenassist aesimc",
    "gated_bmi": "rorx andn movbe xgetbv",
    "gated_avx": "vpxor vpaddd vpsrld vpslld vmovdqu vpor vmovdqa vpand vpaddq vaesenc vpsrlq vpshufb vmovd vpinsrd "
                 "vmovups vpmuludq vpalignr vpclmulqdq vaesdec vpandn vpunpckldq vpsllq vmovaps vinserti128 vpblendd "
                 "vzeroupper vpermq vaesenclast vpunpckhqdq vpshufd vpsrldq vpslldq vprotq vpbroadcastq vprotd "
                 "vpcmpeqd vpaddb vxorps vpcmpgtd vaesdeclast vmovq vzeroall vpermd vextracti128 vpbroadcastd",
}
for _g, _names in GATED.items():
    ELSEWHERE.update({m: "%s: behind a CPUID check the shim answers 'absent'; a decision, not a task "
                         "(docs/handoff/lifter-split.md, section 5)" % _g for m in _names.split()})

PRED = ("eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord")       # immediate 0..7 of 0F C2


def _pred(suffix, prefix):
    return "; ".join("cmp%s%s=%s0F C2 /r ib:%d" % (p, suffix, prefix, k) for k, p in enumerate(PRED))


LANES = """\
- Lane-wise on the whole register: for every lane `k`, `r.<lane>[k] = f(d.<lane>[k], s.<lane>[k])`;
  the result replaces the destination. The source is an xmm register or 16 bytes of memory."""

# name, group, "mnemonic=encoding | encoding; ...", semantics; state, why, model, spellings.
# An encoding is in Intel notation; ":N" after "ib" fixes the immediate that selects the mnemonic.
TASKS = [
    # ---- sse_shuffle ----------------------------------------------------------------------------------------
    dict(name="sse_shift_dq", group="sse_shuffle", state="done", model="sse_mov",
         insns="psrldq=66 0F 73 /3 ib; pslldq=66 0F 73 /7 ib",
         sem="""\
- Both shift the whole 128-bit register by `n = imm8` **bytes**; the operand is an xmm register
  only (there is no memory form), and it is operand 0; the immediate is operand 1.
- `psrldq`: `r.b[k] = (k + n < 16) ? d.b[k + n] : 0` for k = 0..15 (towards lane 0).
- `pslldq`: `r.b[k] = (k >= n) ? d.b[k - n] : 0`.
- `n` above 15 gives all zeros. `n` is known when you translate: `tr.ops[1].imm & 0xFF`."""),
    dict(name="sse_shufps", group="sse_shuffle",
         insns="shufps=0F C6 /r ib; shufpd=66 0F C6 /r ib",
         sem="""\
- `shufps`: `r.d[0] = d.d[imm & 3]; r.d[1] = d.d[(imm >> 2) & 3]; r.d[2] = s.d[(imm >> 4) & 3];
  r.d[3] = s.d[(imm >> 6) & 3]`. The two low lanes come from the destination, the two high ones
  from the source.
- `shufpd`: `r.q[0] = d.q[imm & 1]; r.q[1] = s.q[(imm >> 1) & 1]`; the other immediate bits are ignored.
- These move bits and never look at them as numbers: use the integer lanes `d` and `q`, not `f`/`pd`.
- The source is an xmm register or 16 bytes of memory; the destination may be the source."""),
    dict(name="sse_pshufd", group="sse_shuffle",
         insns="pshufd=66 0F 70 /r ib; pshuflw=F2 0F 70 /r ib; pshufhw=F3 0F 70 /r ib",
         sem="""\
- `pshufd`: `r.d[k] = s.d[(imm >> 2*k) & 3]` for k = 0..3.
- `pshuflw`: `r.w[k] = s.w[(imm >> 2*k) & 3]` for k = 0..3; `r.q[1] = s.q[1]` (copied from the source).
- `pshufhw`: `r.q[0] = s.q[0]`; `r.w[4 + k] = s.w[4 + ((imm >> 2*k) & 3)]` for k = 0..3.
- The destination's old value is not used. The source is an xmm register or 16 bytes of memory."""),
    dict(name="sse_pshufb", group="sse_shuffle", insns="pshufb=66 0F 38 00 /r",
         sem="""\
- For k = 0..15: `r.b[k] = (s.b[k] & 0x80) ? 0 : d.b[s.b[k] & 15]`. The source holds the indices,
  the destination holds the bytes that are picked and receives the result.
- The source is an xmm register or 16 bytes of memory. The form without the 66 prefix is the MMX
  instruction of the same name: put it in `REFUSED` (hex `0f3800ca`)."""),
    dict(name="sse_palignr", group="sse_shuffle", insns="palignr=66 0F 3A 0F /r ib",
         sem="""\
- Put the source below the destination as 32 bytes `t`: `t[0..15] = s.b[0..15]`,
  `t[16..31] = d.b[0..15]`. Then `r.b[k] = (k + n < 32) ? t[k + n] : 0` for k = 0..15, `n = imm8`
  (0 to 255; 32 and above give all zeros).
- The source is an xmm register or 16 bytes of memory. Test `n` = 0, 4, 8, 15, 16, 17, 31, 32."""),
    dict(name="sse_movdup", group="sse_shuffle",
         insns="movddup=F2 0F 12 /r; movshdup=F3 0F 16 /r; movsldup=F3 0F 12 /r",
         sem="""\
- `movddup`: `r.q[0] = r.q[1] = s.q[0]`. Its memory operand is **8 bytes** (`LD64(tr.ea(1))`), not 16.
- `movshdup`: `r.d[0] = r.d[1] = s.d[1]; r.d[2] = r.d[3] = s.d[3]`. Memory operand: 16 bytes.
- `movsldup`: `r.d[0] = r.d[1] = s.d[0]; r.d[2] = r.d[3] = s.d[2]`. Memory operand: 16 bytes."""),
    # ---- sse_unpack -----------------------------------------------------------------------------------------
    dict(name="sse_unpck", group="sse_unpack",
         insns="unpcklps=0F 14 /r; unpckhps=0F 15 /r; unpcklpd=66 0F 14 /r; unpckhpd=66 0F 15 /r",
         sem="""\
- `unpcklps`: `r.d = {d.d[0], s.d[0], d.d[1], s.d[1]}`; `unpckhps`: `r.d = {d.d[2], s.d[2], d.d[3], s.d[3]}`.
- `unpcklpd`: `r.q = {d.q[0], s.q[0]}`; `unpckhpd`: `r.q = {d.q[1], s.q[1]}`.
- Bits are moved, not numbers: use the integer lanes. Source: xmm register or 16 bytes of memory."""),
    dict(name="sse_punpckl", group="sse_unpack",
         insns="punpcklbw=66 0F 60 /r; punpcklwd=66 0F 61 /r; punpckldq=66 0F 62 /r; punpcklqdq=66 0F 6C /r",
         sem="""\
- Interleave the **low** halves, destination first: `punpcklbw`: `r.b[2k] = d.b[k], r.b[2k+1] = s.b[k]`
  for k = 0..7; `punpcklwd`: `r.w[2k] = d.w[k], r.w[2k+1] = s.w[k]` for k = 0..3; `punpckldq`:
  `r.d = {d.d[0], s.d[0], d.d[1], s.d[1]}`; `punpcklqdq`: `r.q = {d.q[0], s.q[0]}`.
- Source: xmm register or 16 bytes of memory. The forms without the 66 prefix are MMX: `REFUSED`."""),
    dict(name="sse_punpckh", group="sse_unpack",
         insns="punpckhbw=66 0F 68 /r; punpckhwd=66 0F 69 /r; punpckhdq=66 0F 6A /r; punpckhqdq=66 0F 6D /r",
         sem="""\
- Interleave the **high** halves, destination first: `punpckhbw`: `r.b[2k] = d.b[8+k], r.b[2k+1] = s.b[8+k]`
  for k = 0..7; `punpckhwd`: `r.w[2k] = d.w[4+k], r.w[2k+1] = s.w[4+k]` for k = 0..3; `punpckhdq`:
  `r.d = {d.d[2], s.d[2], d.d[3], s.d[3]}`; `punpckhqdq`: `r.q = {d.q[1], s.q[1]}`.
- Source: xmm register or 16 bytes of memory. The forms without the 66 prefix are MMX: `REFUSED`."""),
    dict(name="sse_movlh", group="sse_unpack", insns="movlhps=0F 16 /r; movhlps=0F 12 /r",
         sem="""\
- Register to register only. `movlhps`: `d.q[1] = s.q[0]` (d.q[0] stays). `movhlps`: `d.q[0] = s.q[1]`
  (d.q[1] stays).
- The same opcodes with a memory operand are `movhps` and `movlps`, another task's mnemonics: the
  decoder names them, so they never reach your translator."""),
    dict(name="sse_movhl_mem", group="sse_unpack",
         insns="movhps=0F 16 /r | 0F 17 /r; movlps=0F 12 /r | 0F 13 /r; movhpd=66 0F 16 /r | 66 0F 17 /r; "
               "movlpd=66 0F 12 /r | 66 0F 13 /r",
         sem="""\
- One operand is an xmm register, the other **8 bytes of memory**; there is no register-to-register
  form under these names. Read with `LD64(tr.ea(i))`, write with `ST64(tr.ea(i), v)`.
- Load (`0F 16`, `0F 12`; operand 0 is the register): `movhps`/`movhpd`: `d.q[1] = m64`, d.q[0] stays;
  `movlps`/`movlpd`: `d.q[0] = m64`, d.q[1] stays.
- Store (`0F 17`, `0F 13`; operand 0 is the memory): `movhps`/`movhpd`: `m64 = s.q[1]`;
  `movlps`/`movlpd`: `m64 = s.q[0]`."""),
    # ---- sse_mov_misc ---------------------------------------------------------------------------------------
    dict(name="movnti", group="sse_mov_misc", insns="movnti=0F C3 /r | REX.W 0F C3 /r", model="",
         sem="""\
- A plain store of a general register to memory: `m32 = r32` or, with REX.W, `m64 = r64`. The
  "non-temporal" hint changes nothing a program can observe. No flags change.
- Operand 0 is the memory, operand 1 the register; there is no register destination. The whole
  translator is: `b = tr.bits(0)`; refuse unless `tr.is_mem(0)` and `b in (32, 64)`; then
  `return [tr.wr(0, tr.val(1, b), b)]`.
- No xmm operand: register it without `vector=True`, and it needs no C header."""),
    dict(name="sse_lddqu", group="sse_mov_misc",
         insns="lddqu=F2 0F F0 /r; movntps=0F 2B /r; movntdq=66 0F E7 /r",
         sem="""\
- Plain 16-byte moves between an xmm register and memory, never register to register.
- `lddqu xmm, m128`: load, no alignment requirement (`ctx->xmm[d] = LDX(addr)`).
- `movntps m128, xmm` and `movntdq m128, xmm`: store (`STX(addr, ctx->xmm[s])`); operand 0 is the
  memory. The non-temporal hint changes nothing a program can observe."""),
    dict(name="sse_movmsk", group="sse_mov_misc",
         insns="movmskps=0F 50 /r; movmskpd=66 0F 50 /r; pmovmskb=66 0F D7 /r",
         sem="""\
- Destination: a general register (operand 0); source: an xmm register only (operand 1).
- `movmskps`: bit k of the result = bit 31 of `s.d[k]`, k = 0..3. `movmskpd`: bit k = bit 63 of
  `s.q[k]`, k = 0..1. `pmovmskb`: bit k = bit 7 of `s.b[k]`, k = 0..15. All higher bits are 0.
- The decoder prints the destination as a 32-bit register; writing it clears bits 63:32
  (`tr.wr(0, expr, 32)` does that). Refuse any other destination size: `tr.bits(0) != 32`."""),
    dict(name="sse_pinsr", group="sse_mov_misc",
         insns="pinsrw=66 0F C4 /r ib; pinsrb=66 0F 3A 20 /r ib; pinsrd=66 0F 3A 22 /r ib",
         sem="""\
- Replace one lane of the destination, leave the others: `pinsrw`: `d.w[imm & 7] = v`; `pinsrb`:
  `d.b[imm & 15] = v`; `pinsrd`: `d.d[imm & 3] = v`.
- `v` comes from operand 1: a 32-bit general register (its low 16 or 8 bits for `pinsrw`/`pinsrb`:
  `"(uint16_t)%s" % tr.val(1, 32)`) or memory of exactly the lane's size (`tr.val(1, 16)`,
  `tr.val(1, 8)`, `tr.val(1, 32)`). Use `tr.is_mem(1)` and `tr.bits(1)` to tell them apart, and
  refuse anything else. With REX.W the decoder says `pinsrq`: not yours."""),
    dict(name="sse_pextr", group="sse_mov_misc",
         insns="pextrw=66 0F C5 /r ib | 66 0F 3A 15 /r ib; pextrd=66 0F 3A 16 /r ib; pextrq=66 REX.W 0F 3A 16 /r ib",
         sem="""\
- Copy one lane of an xmm register (operand 1) to a general register or memory (operand 0); the
  immediate is operand 2. `pextrw`: `s.w[imm & 7]`; `pextrd`: `s.d[imm & 3]`; `pextrq`: `s.q[imm & 1]`.
- `pextrw` has two encodings. `66 0F C5`: the destination is a general register only. `66 0F 3A 15`:
  a general register or 2 bytes of memory. A register destination receives the value zero-extended
  to the whole 64-bit register (write it with `tr.wr(0, "(uint32_t)...", 32)`; refuse a
  destination that is not 32 bits wide unless you have a vector that tests it); a memory
  destination is written with its own size (`tr.wr(0, expr, 16)`).
- `pextrd`: 32-bit register (bits 63:32 cleared) or 4 bytes of memory. `pextrq`: 64-bit register or
  8 bytes of memory."""),
    # ---- sse_int_arith --------------------------------------------------------------------------------------
    dict(name="sse_padd", group="sse_int_arith",
         insns="paddb=66 0F FC /r; paddw=66 0F FD /r; paddd=66 0F FE /r; paddq=66 0F D4 /r",
         sem=LANES + """
- Wrapping addition on unsigned lanes: `paddb` on `b` (16 lanes), `paddw` on `w` (8), `paddd` on
  `d` (4), `paddq` on `q` (2). In C, add `uint8_t`/`uint16_t` lanes and cast the sum back to the
  lane type (the operands are promoted to int)."""),
    dict(name="sse_psub", group="sse_int_arith",
         insns="psubb=66 0F F8 /r; psubw=66 0F F9 /r; psubd=66 0F FA /r; psubq=66 0F FB /r",
         sem=LANES + """
- Wrapping subtraction `d - s` on unsigned lanes: `psubb` on `b`, `psubw` on `w`, `psubd` on `d`,
  `psubq` on `q`. Cast the difference back to the lane type."""),
    dict(name="sse_pminmax", group="sse_int_arith",
         insns="pmaxsw=66 0F EE /r; pminsw=66 0F EA /r; pmaxub=66 0F DE /r; pminub=66 0F DA /r",
         sem=LANES + """
- `pmaxsw`/`pminsw`: maximum/minimum of **signed** 16-bit lanes (`sw`). `pmaxub`/`pminub`:
  maximum/minimum of **unsigned** 8-bit lanes (`b`)."""),
    dict(name="sse_pminsd", group="sse_int_arith", insns="pminsd=66 0F 38 39 /r",
         sem=LANES + """
- Minimum of **signed** 32-bit lanes (`i`): `r.i[k] = d.i[k] < s.i[k] ? d.i[k] : s.i[k]`, k = 0..3."""),
    dict(name="sse_pmul", group="sse_int_arith",
         insns="pmullw=66 0F D5 /r; pmulhw=66 0F E5 /r; pmulhuw=66 0F E4 /r; pmulld=66 0F 38 40 /r",
         sem=LANES + """
- `pmullw`: low 16 bits of the product of 16-bit lanes: `(uint16_t)((uint32_t)d.w[k] * s.w[k])`.
- `pmulhw`: high 16 bits of the **signed** product: `(uint16_t)(((int32_t)d.sw[k] * (int32_t)s.sw[k]) >> 16)`.
- `pmulhuw`: high 16 bits of the **unsigned** product: `(uint16_t)(((uint32_t)d.w[k] * (uint32_t)s.w[k]) >> 16)`.
- `pmulld`: low 32 bits of the product of 32-bit lanes: `(uint32_t)((uint64_t)d.d[k] * s.d[k])`."""),
    dict(name="sse_pmuldq", group="sse_int_arith", insns="pmuludq=66 0F F4 /r; pmaddubsw=66 0F 38 04 /r",
         sem="""\
- `pmuludq`: `r.q[k] = (uint64_t)d.d[2*k] * (uint64_t)s.d[2*k]` for k = 0..1 (the even 32-bit lanes,
  unsigned, full 64-bit product).
- `pmaddubsw`: for k = 0..7, `t = d.b[2k] * s.sb[2k] + d.b[2k+1] * s.sb[2k+1]` computed in `int`
  (the destination's bytes are **unsigned**, the source's **signed**), then saturated to a signed
  16-bit lane: `r.sw[k] = t > 32767 ? 32767 : t < -32768 ? -32768 : t`.
- Source: xmm register or 16 bytes of memory; the result replaces the destination."""),
    dict(name="sse_pavg", group="sse_int_arith", insns="pavgb=66 0F E0 /r; pavgw=66 0F E3 /r",
         sem=LANES + """
- Rounded average of unsigned lanes, computed without overflow: `pavgb`:
  `(uint8_t)(((unsigned)d.b[k] + s.b[k] + 1) >> 1)`; `pavgw`: `(uint16_t)(((uint32_t)d.w[k] + s.w[k] + 1) >> 1)`."""),
    dict(name="sse_padds", group="sse_int_arith",
         insns="paddsb=66 0F EC /r; paddsw=66 0F ED /r; paddusb=66 0F DC /r; paddusw=66 0F DD /r",
         sem=LANES + """
- Saturating addition, computed in `int` and clamped: `paddsb` signed 8-bit lanes (`sb`, clamp to
  -128..127), `paddsw` signed 16-bit (`sw`, -32768..32767), `paddusb` unsigned 8-bit (`b`, 0..255),
  `paddusw` unsigned 16-bit (`w`, 0..65535)."""),
    dict(name="sse_psubs", group="sse_int_arith",
         insns="psubsb=66 0F E8 /r; psubsw=66 0F E9 /r; psubusb=66 0F D8 /r; psubusw=66 0F D9 /r",
         sem=LANES + """
- Saturating subtraction `d - s`, computed in `int` and clamped: `psubsb` signed 8-bit lanes (`sb`,
  -128..127), `psubsw` signed 16-bit (`sw`), `psubusb` unsigned 8-bit (`b`, a negative difference
  gives 0), `psubusw` unsigned 16-bit (`w`)."""),
    dict(name="sse_psadbw", group="sse_int_arith", insns="psadbw=66 0F F6 /r",
         sem="""\
- For each half h = 0, 1: `r.q[h]` = the sum over k = 0..7 of `|d.b[8h + k] - s.b[8h + k]|` (unsigned
  bytes, absolute difference; the sum is at most 2040, so bits 63:16 of each half are 0).
- Source: xmm register or 16 bytes of memory; the result replaces the destination."""),
    # ---- sse_int_cmp_pack -----------------------------------------------------------------------------------
    dict(name="sse_pcmpeq", group="sse_int_cmp_pack",
         insns="pcmpeqb=66 0F 74 /r; pcmpeqw=66 0F 75 /r; pcmpeqd=66 0F 76 /r",
         sem=LANES + """
- A lane becomes all ones if `d == s` and all zeros otherwise: `pcmpeqb` on `b`, `pcmpeqw` on `w`,
  `pcmpeqd` on `d`. No flags change. `pcmpeqd xmm0, xmm0` (all ones) is common: test a register
  with itself."""),
    dict(name="sse_pcmpgt", group="sse_int_cmp_pack",
         insns="pcmpgtb=66 0F 64 /r; pcmpgtw=66 0F 65 /r; pcmpgtd=66 0F 66 /r",
         sem=LANES + """
- A lane becomes all ones if `d > s` as **signed** numbers and all zeros otherwise: `pcmpgtb` on
  `sb`, `pcmpgtw` on `sw`, `pcmpgtd` on `i`. No flags change."""),
    dict(name="sse_pack", group="sse_int_cmp_pack",
         insns="packuswb=66 0F 67 /r; packsswb=66 0F 63 /r; packssdw=66 0F 6B /r",
         sem="""\
- Narrow with saturation; the low half of the result comes from the destination, the high half
  from the source (xmm register or 16 bytes of memory).
- `packsswb`: `r.sb[k] = sat(d.sw[k])`, `r.sb[8 + k] = sat(s.sw[k])` for k = 0..7, sat clamps a
  signed 16-bit value to -128..127.
- `packuswb`: the same lanes, but clamping the **signed** 16-bit value to 0..255 (negative gives 0).
- `packssdw`: `r.sw[k] = sat(d.i[k])`, `r.sw[4 + k] = sat(s.i[k])` for k = 0..3, clamping a signed
  32-bit value to -32768..32767."""),
    # ---- sse_int_shift --------------------------------------------------------------------------------------
    dict(name="sse_psll", group="sse_int_shift",
         insns="psllw=66 0F F1 /r | 66 0F 71 /6 ib; pslld=66 0F F2 /r | 66 0F 72 /6 ib; "
               "psllq=66 0F F3 /r | 66 0F 73 /6 ib",
         sem="""\
- Shift every lane left by the same count: `psllw` lanes `w` (16 bits), `pslld` lanes `d` (32),
  `psllq` lanes `q` (64). A count above 15 / 31 / 63 gives all zeros; in C a shift by the lane
  width or more is undefined, so test the count first.
- Two forms. With an immediate (`/6 ib`): operand 0 is the xmm register, the count is
  `tr.ops[1].imm & 0xFF`. With a count operand (`/r`): the count is the **whole low 64 bits** of an
  xmm register or of 16 bytes of memory (in C: `"%s.q[0]" % tr.xval(1)`), a `uint64_t` that can be huge.
- Read the count before writing the destination (`psllw xmm1, xmm1` is legal)."""),
    dict(name="sse_psrl", group="sse_int_shift",
         insns="psrlw=66 0F D1 /r | 66 0F 71 /2 ib; psrld=66 0F D2 /r | 66 0F 72 /2 ib; "
               "psrlq=66 0F D3 /r | 66 0F 73 /2 ib",
         sem="""\
- Shift every lane right, filling with zeros: `psrlw` lanes `w` (16 bits), `psrld` lanes `d` (32),
  `psrlq` lanes `q` (64). A count above 15 / 31 / 63 gives all zeros; in C a shift by the lane
  width or more is undefined, so test the count first.
- Two forms. With an immediate (`/2 ib`): operand 0 is the xmm register, the count is
  `tr.ops[1].imm & 0xFF`. With a count operand (`/r`): the count is the **whole low 64 bits** of an
  xmm register or of 16 bytes of memory (in C: `"%s.q[0]" % tr.xval(1)`), a `uint64_t` that can be huge.
- Read the count before writing the destination. `66 0F 73 /3` is `psrldq`, another task's."""),
    dict(name="sse_psra", group="sse_int_shift",
         insns="psraw=66 0F E1 /r | 66 0F 71 /4 ib; psrad=66 0F E2 /r | 66 0F 72 /4 ib",
         sem="""\
- Shift every lane right, filling with the sign bit: `psraw` lanes `sw` (16 bits), `psrad` lanes
  `i` (32). A count above 15 / 31 behaves as 15 / 31 (every bit becomes the sign). Shift a signed
  lane (`int16_t`, `int32_t`) by the clamped count.
- Two forms. With an immediate (`/4 ib`): operand 0 is the xmm register, the count is
  `tr.ops[1].imm & 0xFF`. With a count operand (`/r`): the count is the **whole low 64 bits** of an
  xmm register or of 16 bytes of memory (in C: `"%s.q[0]" % tr.xval(1)`), a `uint64_t` that can be huge.
- Read the count before writing the destination."""),
    # ---- sse_pcmpistri, int_rare ----------------------------------------------------------------------------
    dict(name="sse_pcmpistri", group="sse_pcmpistri", insns="pcmpistri=66 0F 3A 63 /r ib", state="done",
         why="one instruction with 256 control-byte variants (four aggregation modes, two element sizes, "
             "polarity, index selection) that writes ECX and six flags; needs someone who can read the SDM's "
             "tables and decide which control bytes to translate",
         sem="- SSE4.2 implicit-length string compare; see the Intel SDM, section 4.1 and the PCMPISTRI entry."),
    dict(name="int_loop", group="int_rare", insns="loop=E2 cb; loopne=E0 cb", model="control",
         sem="""\
- `loop rel8`: `rcx = rcx - 1` (all 64 bits; no flag changes, unlike `dec`), then jump to the target
  if `rcx != 0`.
- `loopne rel8` (also printed `loopnz`): the same decrement, then jump if `rcx != 0` and `ZF` is 0;
  the condition is `ctx->r[1] != 0 && !ctx->zf`. It reads `ZF` and changes no flag.
- It is a conditional jump, and the control-flow analysis already treats it as one: your
  translator gets the same `flow` as `jrcxz` in `lifter/insns/control.py` (function `t_jcc`). Copy
  its three `return` lines; the statement before the test is `ctx->r[1] -= 1;` and the condition is
  `ctx->r[1] != 0`, so each returned list has two statements.
- Refuse an address-size prefix, which makes the counter ECX: `if tr.ins.addr_size != 8: raise
  Unsupported("loop with a 32-bit counter")`. `loope`/`loopne` are not yours.
- No xmm operand, no C header. Vectors (the target must be another slot of your image or 0xf00;
  a forward jump over one slot is `e20e` at slot S, which the decoder prints as `loop S+0x10`):
  write them with `tools/insn_vec.py e20e --at 0x1000`, and one with the prefix, `67e20d`, in
  `REFUSED`."""),
    dict(name="int_pushf", group="int_rare", insns="pushfq=9C; popfq=9D", state="done",
         why="RFLAGS has bits Ctx does not hold (IF is 1 in a Windows process and 0 in the oracle's start state; "
             "TF, AC, ID, IOPL): which value pushfq stores and what popfq does with them is a decision and a "
             "harness change, in files lifter-float-infra and lifter-core-fixes own",
         sem="- `pushfq` pushes RFLAGS, `popfq` pops it; DF is among the bits."),
    # ---- float groups: held until FLOAT_PATTERN is filled ---------------------------------------------------
    # (sse_arith_ss and sse_arith_sd are cut together, by operation: each task has the single and the double form)
    dict(name="sse_mul_s", group="sse_arith_ss", insns="mulss=F3 0F 59 /r; mulsd=F2 0F 59 /r",
         sem="- Scalar multiply of the low single (`mulss`) or double (`mulsd`); the upper lanes of the destination stay."),
    dict(name="sse_sub_s", group="sse_arith_ss", insns="subss=F3 0F 5C /r; subsd=F2 0F 5C /r",
         sem="- Scalar subtract `d - s` of the low single or double; the upper lanes of the destination stay."),
    dict(name="sse_div_s", group="sse_arith_ss", insns="divss=F3 0F 5E /r; divsd=F2 0F 5E /r",
         sem="- Scalar divide `d / s` of the low single or double; the upper lanes of the destination stay."),
    dict(name="sse_minmax_s", group="sse_arith_ss", state="done",
         insns="minss=F3 0F 5D /r; maxss=F3 0F 5F /r; minsd=F2 0F 5D /r; maxsd=F2 0F 5F /r",
         sem="- Scalar minimum and maximum. Not IEEE min/max: if either operand is a NaN, or both are zeros, "
             "the result is the **source** (second) operand."),
    dict(name="sse_sqrt_s", group="sse_arith_ss", insns="sqrtss=F3 0F 51 /r; sqrtsd=F2 0F 51 /r",
         sem="- Scalar square root of the source's low single or double into the destination's low lane; the upper lanes stay."),
    dict(name="sse_addsub_p", group="sse_arith_packed", state="done",
         insns="addps=0F 58 /r; subps=0F 5C /r; addpd=66 0F 58 /r; subpd=66 0F 5C /r",
         sem="- Packed add and subtract on 4 singles or 2 doubles."),
    dict(name="sse_muldiv_p", group="sse_arith_packed",
         insns="mulps=0F 59 /r; divps=0F 5E /r; mulpd=66 0F 59 /r; divpd=66 0F 5E /r",
         sem="- Packed multiply and divide on 4 singles or 2 doubles."),
    dict(name="sse_minmax_p", group="sse_arith_packed",
         insns="minps=0F 5D /r; maxps=0F 5F /r; minpd=66 0F 5D /r; maxpd=66 0F 5F /r",
         sem="- Packed minimum and maximum, per lane with the rule of `minss`: a NaN or two zeros give the source operand."),
    dict(name="sse_sqrt_p", group="sse_arith_packed", insns="sqrtps=0F 51 /r; sqrtpd=66 0F 51 /r",
         sem="- Packed square root of every lane of the source."),
    dict(name="sse_hadd", group="sse_arith_packed", state="done", insns="addsubps=F2 0F D0 /r; haddps=F2 0F 7C /r",
         sem="- `addsubps`: lanes 0 and 2 are `d - s`, lanes 1 and 3 are `d + s`. `haddps`: "
             "`r.f = {d0 + d1, d2 + d3, s0 + s1, s2 + s3}`."),
    dict(name="sse_comi", group="sse_comi",
         insns="comiss=0F 2F /r; ucomiss=0F 2E /r; comisd=66 0F 2F /r; ucomisd=66 0F 2E /r",
         sem="- Compare the low single or double and set ZF, PF, CF (unordered: 1 1 1; greater: 0 0 0; less: 0 0 1; "
             "equal: 1 0 0); OF, SF and AF become 0. `comi*` and `ucomi*` differ only in which NaNs raise the "
             "invalid exception."),
    dict(name="sse_cmpps", group="sse_cmp_pred", state="done", insns=_pred("ps", ""), spellings=True,
         sem="- One instruction, `0F C2 /r ib`, whose immediate 0..7 selects the predicate and the mnemonic the "
             "decoder prints; every lane becomes all ones or all zeros. With an immediate above 7 the decoder "
             "prints `cmpps`, which stays untranslated."),
    dict(name="sse_cmpss", group="sse_cmp_pred", state="done", insns=_pred("ss", "F3 "), spellings=True,
         sem="- As `sse_cmpps` on the low single only; the destination's upper lanes stay."),
    dict(name="sse_cmppd", group="sse_cmp_pred", insns=_pred("pd", "66 "), spellings=True,
         sem="- As `sse_cmpps` on 2 doubles. Not in the game; the corpus has it."),
    dict(name="sse_cmpsd", group="sse_cmp_pred", state="done", insns=_pred("sd", "F2 "), spellings=True,
         sem="- As `sse_cmpps` on the low double only. Not in the game; the corpus has it."),
    dict(name="sse_cvt_pd_ps", group="sse_cvt_fp", insns="cvtps2pd=0F 5A /r; cvtpd2ps=66 0F 5A /r",
         sem="- `cvtps2pd`: the 2 low singles to 2 doubles (memory operand: 8 bytes). `cvtpd2ps`: 2 doubles to 2 "
             "singles in the low half, the high half becomes 0; rounds."),
    dict(name="sse_cvt_sd_ss", group="sse_cvt_fp", insns="cvtss2sd=F3 0F 5A /r; cvtsd2ss=F2 0F 5A /r",
         sem="- Scalar single to double and double to single (rounds); the destination's upper lanes stay."),
    dict(name="sse_cvtsi2s", group="sse_cvt_int",
         insns="cvtsi2ss=F3 0F 2A /r | F3 REX.W 0F 2A /r; cvtsi2sd=F2 0F 2A /r | F2 REX.W 0F 2A /r",
         sem="- Signed 32-bit or (REX.W) 64-bit integer, register or memory, to the low single or double; rounds "
             "when the integer has more bits than the mantissa."),
    dict(name="sse_cvtt_s2si", group="sse_cvt_int",
         insns="cvttss2si=F3 0F 2C /r | F3 REX.W 0F 2C /r; cvttsd2si=F2 0F 2C /r | F2 REX.W 0F 2C /r",
         sem="- Low single or double to a signed 32-bit or 64-bit integer, truncating; out of range or NaN gives "
             "the 'integer indefinite' value (0x80000000 or 0x8000000000000000)."),
    dict(name="sse_cvt_s2si", group="sse_cvt_int",
         insns="cvtss2si=F3 0F 2D /r | F3 REX.W 0F 2D /r; cvtsd2si=F2 0F 2D /r | F2 REX.W 0F 2D /r",
         sem="- As `sse_cvtt_s2si`, but rounding in the mode MXCSR selects."),
    dict(name="sse_cvtdq2p", group="sse_cvt_int", insns="cvtdq2ps=0F 5B /r; cvtdq2pd=F3 0F E6 /r",
         sem="- `cvtdq2ps`: 4 signed 32-bit integers to singles (rounds). `cvtdq2pd`: the 2 low integers to "
             "doubles (exact; memory operand: 8 bytes)."),
    dict(name="sse_cvtp2dq", group="sse_cvt_int",
         insns="cvtps2dq=66 0F 5B /r; cvttps2dq=F3 0F 5B /r; cvtpd2dq=F2 0F E6 /r; cvttpd2dq=66 0F E6 /r",
         sem="- Packed singles or doubles to signed 32-bit integers, rounding (`cvt`) or truncating (`cvtt`); the "
             "double forms fill the low half and zero the high half; 'integer indefinite' on overflow or NaN."),
    # Was held for a stronger model (which table to reproduce, and only the real CPU can test it).
    # Done by unit lifter-recip: Intel's table, which the host's frecpe/frsqrte is under FPCR.AH.
    dict(name="sse_recip", group="sse_recip", state="done",
         insns="rcpps=0F 53 /r; rcpss=F3 0F 53 /r; rsqrtps=0F 52 /r; rsqrtss=F3 0F 52 /r",
         sem="- Approximate reciprocal and reciprocal square root (relative error at most 1.5 * 2^-12): a table "
             "inside the CPU, Intel's here; judged by the real CPU only."),
    dict(name="fp_x87cw", group="fp_control", insns="fnstcw=D9 /7; fldcw=D9 /5; fnclex=DB E2", state="done",
         why="the x87 control word needs a field in Ctx and a place in the harness's state comparison: a change "
             "of the core, which lifter-float-infra owns",
         sem="- Store and load the x87 control word; clear the x87 exception flags."),
]

BRIEF = """# Unit insn-{name}: {mn_list}

This repository recompiles the x86-64 code of a Windows game into C, one instruction at a time
(`lifter/`), and tests every translation against an emulator, the *oracle*. Instructions are
grouped into small *families*: three files each, which nobody else edits. Write the family
`{name}`: {mn_list}.

## Files (create exactly these; edit nothing else, not even to fix something)

- `lifter/insns/{name}.py`: the translators.
- `harness/insns/{name}.h`: C helpers for them. Leave the file out if you need none.
- `tests/vectors/{name}.py`: the test vectors.

## Read first, and nothing else unless you are stuck

1. `lifter/insns/README.md` (one page): the rules of a family. Follow it exactly.
{model_read}

Do not read `lifter/translate.py`, `harness/difftest.py` or `tools/family_check.py`: what you
need from them is on this page.

## The instructions

| Mnemonic | Encoding (Intel notation) | Register it with | Instances in the game |
|---|---|---|---|
{insn_rows}
{shapes}
What they do (from the Intel manual, volume 2; `d` is the destination before the instruction,
`s` the source, `r` the result, lanes as in the `Xmm` union below):

{sem}

If you have a tool that fetches web pages, the manual's entry is
`https://www.felixcloutier.com/x86/<mnemonic>`. The manual is the specification: if it and this
summary disagree, or the oracle and the manual disagree, stop and say so in your reply.
{float_section}
## What a translator may use

A translator is `def t_name(tr, flow):` and returns a list of C statements (strings); they are
put in one `{{ }}` block, so short local names are fine. Raise `Unsupported("why")` for every
form you do not translate.

```
from ..translate import Family, T, Unsupported, enc, lit
tr.need_ops(n)        refuse unless the instruction has n operands (immediates count)
tr.ops[i].imm         operand i's immediate, a Python int       tr.name   the mnemonic
tr.is_reg(i), tr.is_mem(i), tr.bits(i)     kind and size in bits of operand i
tr.xr(i)              number of the xmm register operand i is, or None
tr.xval(i)            C expression of type Xmm: the xmm register, or the 16 bytes at a memory
                      operand. Only for 16-byte memory operands; smaller ones: LD64(tr.ea(i))
tr.ea(i)              name of a C temporary (uint64_t) holding memory operand i's address
tr.val(i, bits)       C expression uint<bits>_t: a general register, immediate or memory
                      operand of exactly that size (refuses any other size)
tr.wr(i, expr, bits)  C statement that stores expr to a general register or memory operand;
                      a 32-bit register write clears bits 63:32
T(bits)               "uint32_t" and so on              lit(v, bits)   a C literal
```

C side (`harness/runtime.h`): `ctx->xmm[n]` is an `Xmm`, a union of lanes with lane 0 at the low
end: `b[16] w[8] d[4] q[2]` unsigned, `sb[16] sw[8] i[4] l[2]` signed. `LD8 LD16 LD32 LD64
LDX(addr)` read memory, `ST8 ST16 ST32 ST64 STX(addr, value)` write it (`LDX`/`STX`: an `Xmm`).
Flags are `ctx->cf pf af zf sf of`; an instruction that changes no flag touches none of them.

Put lane loops in the header as `TF_INLINE` functions on `Xmm` values, for example `Xmm
x_paddw(Xmm d, Xmm s)`, and call them from the translator: `["Xmm s = %s;" % tr.xval(1),
"ctx->xmm[%d] = x_paddw(ctx->xmm[%d], s);" % (d, d)]`. An immediate is known when you translate:
pass it as a number, as the model family does. Always read the source into a local before
writing the destination: they can be the same register. Every name your header defines must
start with `x_<mnemonic>` or
`x_{name}_` (macros: `TF_{NAME}_`), and its include guard is `TF_INSNS_{NAME}_H`: every
header is included in every generated C file, and the check refuses names that could clash.

## Vectors

Do not assemble by hand. This prints ready lines for `VECTORS`, one per operand form, with the
decoder's text and, in the comment, the lifter's name for the instruction and the `enc(...)`
that matches the bytes:

{vec_cmds}

Keep the lines whose comment names your mnemonic (a form that does not exist decodes as
another instruction or not at all), give each line its own slot (`--at 0x1100` sets the first;
16 bytes per line, 0x1000 to 0x4ff0), and drop the comments or keep them. Each mnemonic needs
every operand form the game has (the shapes above), a register with itself where the
instruction has two register operands, and one register numbered 8 or above (the form `hi`).
`--imm 0,1b,ff` chooses immediates (hex); with `--forms rr,hi` every listed form is printed with
every immediate. `tools/insn_vec.py <hex> ...` shows what bytes you wrote yourself decode to.

## Rules that decide pass or fail

- Register exactly these mnemonics: {mn_plain}. Not fewer, and no other: every other
  mnemonic is somebody else's task, and two families with one mnemonic break the build.
- Register only the encodings in the table, with `mand=` as shown (it keeps the MMX and other
  instructions of the same name away from your translator).
- Never guess. A form you cannot test, or a result you are not sure of, is `raise
  Unsupported(...)` and one line in your reply.
- Edit no file but your three. If something outside them is wrong or missing, stop and say so.

## Done

Both exit 0 (the first takes three to five minutes; pipe it through `tail -15`, and its last
line must say `result=PASS`):

{check_block}

`make test 2>&1 | tail -15` is the quick check while you work (one minute). When both pass,
commit your files with the message `{name}: {mn_plain}` and reply with the `FAMILY-CHECK` line,
every form you refuse, and anything you were unsure of.
"""

MODEL_PASTE = """2. The finished family `{model}`, pasted here; copy its shape. Do not open other families.

`lifter/insns/{model}.py`:

```python
{py}
```
{hdr}
`tests/vectors/{model}.py`:

```python
{vec}
```"""

MODEL_NAMED = """2. The finished family `{model}`: `lifter/insns/{model}.py`{hdr} and
   `tests/vectors/{model}.py` ({lines} lines together). Copy its shape; do not open other families."""

FLOAT_HELD = """
## How a float instruction is written

(Not written yet: FLOAT_PATTERN in tools/swarm/make_insn_tasks.py is to be filled from
docs/handoff/lifter-float-infra.md. Until then this task is not handed out.)
"""


# ---- the table as data ---------------------------------------------------------------------------------

def parse_insns(text):
    """[(mnemonic, [(encoding, fixed immediate or None), ...])] from "name=enc | enc; name=enc"."""
    out = []
    for part in text.split(";"):
        name, encs = part.strip().split("=", 1)
        items = []
        for e in encs.split("|"):
            e = e.strip()
            m = re.fullmatch(r"(.*\bib):(\d+)", e)
            items.append((m.group(1), int(m.group(2))) if m else (e, None))
        out.append((name.strip(), items))
    return out


def tasks():
    out = []
    for t in TASKS:
        t = dict(t)
        t["insns"] = parse_insns(t["insns"])
        t["mnemonics"] = [m for m, _ in t["insns"]]
        t.setdefault("state", "todo")
        t["float"] = t["group"] in FLOAT_GROUPS
        if "model" not in t:
            packed = any(re.search(r"p[sd]$|dq$|2p[sd]$|2dq$", m) for m in t["mnemonics"])
            t["model"] = (FLOAT_PACKED_MODEL if packed else FLOAT_MODEL) if t["float"] else VECTOR_MODEL
        t["held"] = t["state"] == "todo" and t["float"] and (FLOAT_PATTERN is None or t["name"] not in FLOAT_RELEASED)
        out.append(t)
    return out


def enc_call(encoding):
    """The enc(...) call that registers an encoding written in Intel notation."""
    import insn_vec
    prefix, w, opcode, digit, imm = insn_vec.parse_enc(encoding) if re.search(r"/[r0-7]", encoding) else (
        [], False, [int(x, 16) for x in encoding.split() if re.fullmatch(r"[0-9A-Fa-f]{2}", x)], None, False)
    if opcode[0] != 0x0F:
        return "enc(0x%02X%s)" % (opcode[0], "" if digit is None else ", reg=%d" % digit)
    key = "0x" + "".join("%02X" % b for b in (opcode[:3] if opcode[1] in (0x38, 0x3A) else opcode[:2]))
    return "enc(%s%s, mand=%s)" % (key, "" if digit is None else ", reg=%d" % digit,
                                   "0x%02X" % prefix[-1] if prefix else "0")


def decoded_names(encoding, imm):
    """The lifter's names for the operand forms of an encoding, as tools/insn_vec.py builds them."""
    import capstone
    import insn_vec
    from lifter import translate as T
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    names = set()
    if not re.search(r"/[r0-7]", encoding):
        raw = bytes(int(x, 16) if re.fullmatch(r"[0-9A-Fa-f]{2}", x) else 0x0E for x in encoding.split())
        ins = next(md.disasm(raw, 0x1000, 1), None)
        return {T.mnemonic_of(ins)} if ins is not None and ins.size == len(raw) else set()
    prefix, w, opcode, digit, has_imm = insn_vec.parse_enc(encoding)
    for form in insn_vec.FORMS:
        raw = insn_vec.encode(prefix, w, opcode, digit if digit is not None else 1, form,
                              (imm if imm is not None else 0x1B) if has_imm else None, 0x1000)
        ins = next(md.disasm(raw, 0x1000, 1), None)
        if ins is not None and ins.size == len(raw):
            names.add(T.mnemonic_of(ins))
    return names


# ---- the instruction index -----------------------------------------------------------------------------

class Index:
    """Counts from the instruction index (tools/census/insn_index.py), or nothing if it cannot be loaded."""

    def __init__(self):
        self.ok = False
        self.sets = {}
        self.entries = []
        try:
            import insn_index
            self.entries = list(insn_index.load(quiet=True).values())
        except (Exception, SystemExit) as e:
            self.error = "%s: %s" % (type(e).__name__, e)
            return
        self.ok = bool(self.entries)
        self.error = "" if self.ok else "the index is empty (no game/ or tests/corpus/build/ here)"
        for e in self.entries:
            if e["group"] == "game":
                for blockers, c in e["blocker_sets"]:
                    key = frozenset(blockers)
                    self.sets[key] = self.sets.get(key, 0) + c

    def game(self, m):
        return sum(e["mnemonics"].get(m, {}).get("count", 0) for e in self.entries if e["group"] == "game")

    def anywhere(self, m):
        return sum(e["mnemonics"].get(m, {}).get("count", 0) for e in self.entries if e["group"] != "redist")

    def reached_in_game(self):
        names = set()
        for e in self.entries:
            if e["group"] == "game":
                names.update(m for m, d in e["mnemonics"].items() if d["reached"])
        return names

    def shapes(self, m):
        """{operand shape: instances} over the game and the corpus."""
        out = {}
        for e in self.entries:
            if e["group"] == "redist":
                continue
            for s, d in e["mnemonics"].get(m, {}).get("shapes", {}).items():
                s = s.split(" ", 1)[1] if " " in s else "(no operands)"
                out[s] = out.get(s, 0) + d["count"]
        return out

    def blocked(self, names):
        names = set(names)
        return sum(c for k, c in self.sets.items() if k & names)

    def unblocked(self, names):
        """Game functions whose untranslated mnemonics (beyond the first lifter's 168) are all in `names`."""
        names = set(names)
        return sum(c for k, c in self.sets.items() if k <= names)


def ordered(ts, idx):
    """The tasks in the order they should be done: the groups without float first, then the float
    groups; inside each, greedily by how many game functions become translatable when the task is
    added to those before it, then by how many functions contain one of its mnemonics."""
    if not idx.ok:
        return list(ts)
    done, out = set(), []
    for is_float in (False, True):
        rest = [t for t in ts if t["float"] == is_float]
        if is_float:                                  # what lifter-float-infra translates comes first
            done |= {m for m, why in ELSEWHERE.items() if "lifter-float-infra" in why}
        while rest:
            base = idx.unblocked(done)
            best = max(rest, key=lambda t: (idx.unblocked(done | set(t["mnemonics"])) - base,
                                            idx.blocked(t["mnemonics"]), sum(idx.game(m) for m in t["mnemonics"])))
            best["gain"] = idx.unblocked(done | set(best["mnemonics"])) - base
            done |= set(best["mnemonics"])
            best["cumulative"] = idx.unblocked(done)
            out.append(best)
            rest.remove(best)
    return out


# ---- briefs and unit files -----------------------------------------------------------------------------

def read(rel):
    with open(os.path.join(ROOT, rel)) as f:
        return f.read().rstrip("\n")


def model_text(model):
    if not model:
        return "2. Nothing else: the whole translator is described below."
    py, vec, hdr = "lifter/insns/%s.py" % model, "tests/vectors/%s.py" % model, "harness/insns/%s.h" % model
    has_hdr = os.path.exists(os.path.join(ROOT, hdr))
    if not os.path.exists(os.path.join(ROOT, py)):
        return "2. The finished family `%s` (not in this tree yet)." % model
    lines = sum(read(p).count("\n") + 1 for p in [py, vec] + ([hdr] if has_hdr else []))
    if lines > 130:
        return MODEL_NAMED.format(model=model, lines=lines, hdr=", `%s`" % hdr if has_hdr else "")
    return MODEL_PASTE.format(model=model, py=read(py), vec=read(vec),
                              hdr="\n`%s`:\n\n```c\n%s\n```\n" % (hdr, read(hdr)) if has_hdr else "")


def checks(t, base):
    return ["make family-check FAMILY=%s%s" % (t["name"], " BASE=" + base if base else ""),
            ".venv/bin/python tools/swarm/make_insn_tasks.py --verify %s" % t["name"]]


def build(t, idx, rank, base=None):
    name = t["name"]
    rows, cmds, shape_lines = [], [], []
    for m, encs in t["insns"]:
        for k, (e, imm) in enumerate(encs):
            note = "" if imm is None else " (immediate %d)" % imm
            rows.append("| `%s` | `%s`%s | `%s` | %s |" % (m, e, note, enc_call(e),
                                                         ("%d" % idx.game(m) if idx.ok else "?") if k == 0 else ""))
            if re.search(r"/[r0-7]", e):
                cmds.append('    .venv/bin/python tools/insn_vec.py --enc "%s"%s --name %s' % (
                    e, "" if imm is None else " --imm %x" % imm, m))
        if idx.ok:
            sh = sorted(idx.shapes(m).items(), key=lambda kv: (-kv[1], kv[0]))
            shape_lines.append("- `%s`: %s" % (m, ", ".join("`%s` %d" % kv for kv in sh) or "no instance anywhere"))
    shapes = ""
    if shape_lines:
        shapes = ("\nOperand shapes among the instances in the game and the test corpus, with their counts (`x` an xmm\n"
                  "register, `m128` 16 bytes of memory, `rip` RIP-relative, `r32` a general register, `i` an immediate):\n\n"
                  + "\n".join(shape_lines) + "\n")
    cs = checks(t, base)
    brief = BRIEF.format(
        name=name, NAME=name.upper(), mn_list=", ".join("`%s`" % m for m in t["mnemonics"]),
        mn_plain=", ".join(t["mnemonics"]), model_read=model_text(t["model"]), insn_rows="\n".join(rows),
        shapes=shapes, sem=t["sem"],
        float_section="" if not t["float"] else (
            FLOAT_HELD if FLOAT_PATTERN is None else "\n## How a float instruction is written\n\n%s\n" % FLOAT_PATTERN),
        vec_cmds="\n".join(cmds) or "    (the vectors are given above)",
        check_block="\n".join("    " + c + (" 2>&1 | tail -15" if c.startswith("make") else "") for c in cs))
    unit = {"id": "insn-" + name, "kind": "insn-task", "issue": 15, "model": "haiku", "priority": 10 + rank,
            "title": "Instructions: %s (family %s)" % (", ".join(t["mnemonics"]), name),
            "needs": ["insn-tasks"] + (["lifter-float-infra"] if t["float"] else []), "check": cs, "state": "todo"}
    return brief, unit


# ---- commands ------------------------------------------------------------------------------------------

def registry():
    """{family: [mnemonics]} of the lifter in this tree, read in a process of its own."""
    r = subprocess.run([sys.executable, "-c", "import json; from lifter import translate as T; "
                        "print(json.dumps({n: f.mnemonics for n, f in T.FAMILIES.items()}))"],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("the lifter does not import:\n" + r.stderr.strip()[-1500:])
    return json.loads(r.stdout)


def cmd_check(ts, idx):
    bad = []
    fams = registry()
    owner = {m: f for f, ms in fams.items() for m in ms}
    seen, names = {}, set()
    for t in ts:
        n = t["name"]
        if not re.fullmatch(r"[a-z][a-z0-9_]*", n) or n in names:
            bad.append("%s: the name is not a new Python module name" % n)
        names.add(n)
        if not t["mnemonics"] or (len(t["mnemonics"]) > 4 and not t.get("spellings")):
            bad.append("%s: %d mnemonics (a task has one to four)" % (n, len(t["mnemonics"])))
        if t["state"] not in ("todo", "done", "strong") or (t["state"] == "strong" and not t.get("why")):
            bad.append("%s: state %r, or a strong task without a reason" % (n, t["state"]))
        for m, encs in t["insns"]:
            if m in seen:
                bad.append("%s: %s is also in %s" % (n, m, seen[m]))
            seen[m] = n
            if m in ELSEWHERE:
                bad.append("%s: %s is listed as somebody else's (%s)" % (n, m, ELSEWHERE[m]))
            for e, imm in encs:
                got = decoded_names(e, imm)
                if m not in got:
                    bad.append("%s: the encoding %r of %s decodes as %s" % (n, e, m, ", ".join(sorted(got)) or "nothing"))
            if m in owner and owner[m] != n:
                bad.append("%s: %s is already translated by the family %s" % (n, m, owner[m]))
        # the family name is unused, or the family there is this task's
        if n in fams:
            extra = sorted(set(fams[n]) - set(t["mnemonics"]))
            if extra:
                bad.append("%s: lifter/insns/%s.py exists and registers %s, which is not this task's" % (n, n, " ".join(extra)))
            if t["state"] == "done" and set(fams[n]) != set(t["mnemonics"]):
                bad.append("%s: marked done, but the family lacks %s" % (n, " ".join(sorted(set(t["mnemonics"]) - set(fams[n])))))
        else:
            if t["state"] == "done":
                bad.append("%s: marked done, but there is no lifter/insns/%s.py" % (n, n))
            for rel in ("harness/insns/%s.h" % n, "tests/vectors/%s.py" % n):
                if os.path.exists(os.path.join(ROOT, rel)):
                    bad.append("%s: %s exists without the family" % (n, rel))
        if t["state"] == "todo" and not t["held"] and t["model"]:
            for rel in ("lifter/insns/%s.py" % t["model"], "tests/vectors/%s.py" % t["model"]):
                if not os.path.exists(os.path.join(ROOT, rel)):
                    bad.append("%s: its model family has no %s" % (n, rel))
    note = ""
    if idx.ok:
        todo = idx.reached_in_game() - set(owner)
        for m in sorted(todo - set(seen) - set(ELSEWHERE)):
            bad.append("%s: reached in the game, not translated, and in no task (%d instances)" % (m, idx.game(m)))
        for m in sorted(m for m in seen if not idx.anywhere(m)):
            bad.append("%s (%s): no instance in the game or the corpus" % (m, seen[m]))
    else:
        note = "; the instruction index could not be read (%s), so coverage of the work list was not checked" % idx.error
    for b in bad:
        print("  wrong: " + b)
    n = lambda f: sum(1 for t in ts if f(t))                                    # noqa: E731
    print("%d mini-families, %d mnemonics: %d to hand out, %d held for the float pattern, %d need a stronger model, "
          "%d done; %d mnemonics are somebody else's%s" % (
              len(ts), len(seen), n(lambda t: t["state"] == "todo" and not t["held"]), n(lambda t: t["held"]),
              n(lambda t: t["state"] == "strong"), n(lambda t: t["state"] == "done"), len(ELSEWHERE), note))
    print("CHECK %s: %d problem%s" % ("FAILED" if bad else "PASSED", len(bad), "" if len(bad) == 1 else "s"))
    return 1 if bad else 0


def state_text(t):
    return "held: float" if t["held"] else "stronger model" if t["state"] == "strong" else t["state"]


def cmd_list(ts, idx):
    print("%3s %-15s %-17s %-15s %9s %8s %7s %6s  %s" % ("#", "name", "group", "state", "instances", "blocked",
                                                       "unblks", "cumul", "mnemonics (instances in the game)"))
    for k, t in enumerate(ordered(ts, idx), 1):
        ms = ", ".join("%s %d" % (m, idx.game(m)) if idx.ok else m for m in t["mnemonics"])
        print("%3d %-15s %-17s %-15s %9s %8s %7s %6s  %s" % (
            k, t["name"], t["group"], state_text(t),
            sum(idx.game(m) for m in t["mnemonics"]) if idx.ok else "?",
            idx.blocked(t["mnemonics"]) if idx.ok else "?", t.get("gain", "?"), t.get("cumulative", "?"), ms))
    if idx.ok:
        for label, sel in (("no float infrastructure needed", lambda t: not t["float"]), ("float", lambda t: t["float"])):
            g = [t for t in ts if sel(t)]
            ms = {m for t in g for m in t["mnemonics"]}
            print("%s: %d mini-families, %d mnemonics, %d instances in the game; alone they unblock %d game functions" % (
                label, len(g), len(ms), sum(idx.game(m) for m in ms), idx.unblocked(ms)))
        every = {m for t in ts for m in t["mnemonics"]} | {m for m, why in ELSEWHERE.items() if "lifter-float-infra" in why}
        print("all of them with addss, addsd, stmxcsr, ldmxcsr: %d of the %d game functions that have a blocker" % (
            idx.unblocked(every), sum(idx.sets.values())))
    print("blocked: game functions containing one of the mnemonics; unblks: functions that become translatable when "
          "the task is added to those above it; cumul: the running total")
    return 0


def cmd_verify(ts, name):
    t = next((t for t in ts if t["name"] == name or "insn-" + t["name"] == name), None)
    if t is None:
        print("VERIFY FAILED: no task %r in tools/swarm/make_insn_tasks.py" % name)
        return 1
    fams = registry()
    got, want = set(fams.get(t["name"], ())), set(t["mnemonics"])
    if t["name"] not in fams:
        print("VERIFY FAILED: there is no family %s (lifter/insns/%s.py)" % (t["name"], t["name"]))
        return 1
    if got != want:
        print("VERIFY FAILED: the family %s must register exactly: %s" % (t["name"], " ".join(sorted(want))))
        if want - got:
            print("  missing: " + " ".join(sorted(want - got)))
        if got - want:
            print("  not this task's (remove them): " + " ".join(sorted(got - want)))
        return 1
    print("VERIFY PASSED: the family %s registers exactly %s" % (t["name"], " ".join(t["mnemonics"])))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", metavar="DIR", default=os.path.join(ROOT, "swarm"),
                    help="write DIR/briefs/insn-<name>.md and DIR/units/insn-<name>.json (default: swarm/)")
    ap.add_argument("--base", metavar="BRANCH", help="add BASE=BRANCH to the family-check commands")
    ap.add_argument("--show", metavar="NAME", help="print one brief")
    ap.add_argument("--list", action="store_true", help="the table, in the order of work")
    ap.add_argument("--check", action="store_true", help="verify the table")
    ap.add_argument("--verify", metavar="NAME", help="the family registers exactly its task's mnemonics")
    args = ap.parse_args()
    ts = tasks()
    if args.verify:
        return cmd_verify(ts, args.verify)
    idx = Index()
    if args.check:
        return cmd_check(ts, idx)
    if args.list:
        return cmd_list(ts, idx)
    order = ordered(ts, idx)
    if args.show:
        for rank, t in enumerate(order, 1):
            if args.show in (t["name"], "insn-" + t["name"]):
                print(build(t, idx, rank, args.base)[0])
                return 0
        sys.exit("no task %r" % args.show)
    if not idx.ok:
        print("note: the instruction index could not be read (%s): briefs lack counts and operand shapes" % idx.error)
    for d in ("briefs", "units"):
        os.makedirs(os.path.join(args.out, d), exist_ok=True)
    held = skipped = 0
    for rank, t in enumerate(order, 1):
        if t["held"]:
            held += 1
            continue
        if t["state"] != "todo":
            skipped += 1
            continue
        brief, unit = build(t, idx, rank, args.base)
        with open(os.path.join(args.out, "briefs", unit["id"] + ".md"), "w") as f:
            f.write(brief)
        path = os.path.join(args.out, "units", unit["id"] + ".json")
        if os.path.exists(path):
            continue
        with open(path, "w") as f:
            json.dump(unit, f, indent=2)
            f.write("\n")
        print("wrote %s (priority %d, %d tokens of brief, roughly)" % (unit["id"], unit["priority"], len(brief) // 4))
    print("%d float tasks held until FLOAT_PATTERN is filled; %d done or for a stronger model" % (held, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
