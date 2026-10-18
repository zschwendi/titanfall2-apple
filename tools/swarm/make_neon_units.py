#!/usr/bin/env python3
"""neon_brief.py: write swarm/briefs/neon-<family>.md and swarm/units/neon-<family>.json for the packed rewrites."""
import json, sys, os
ROWS = {
 "sse_muldiv_p": ("mulps, mulpd, divps, divpd",
   "`mulps` is `tf_fmul_ps`, `mulpd` `tf_fmul_pd`, `divps` `tf_fdiv_ps`, `divpd` `tf_fdiv_pd` (a = the destination register, b = the source)."),
 "sse_minmax_p": ("minps, minpd, maxps, maxpd",
   "`minps` is `tf_fmin_ps`, `minpd` `tf_fmin_pd`, `maxps` `tf_fmax_ps`, `maxpd` `tf_fmax_pd` (a = the destination register, b = the source: the order decides which operand a NaN or two zeros return, keep it)."),
 "sse_sqrt_p": ("sqrtps, sqrtpd",
   "`sqrtps` is `tf_fsqrt_ps(s)`, `sqrtpd` `tf_fsqrt_pd(s)` of the SOURCE operand, written to the destination register."),
 "sse_recip": ("rcpps, rsqrtps (the packed ones only; leave rcpss and rsqrtss exactly as they are)",
   "`rcpps` is `tf_frecpe_ps(s)`, `rsqrtps` `tf_frsqrte_ps(s)` of the SOURCE operand, written to the destination register. The scalar forms of the family keep their code."),
 "sse_shufps": ("shufps, shufpd",
   "`shufps` with immediate n is `TF_SHUF4(ctx->xmm[d], s, n & 3, (n >> 2) & 3, 4 + ((n >> 4) & 3), 4 + ((n >> 6) & 3))` with the four numbers computed in Python and written as literals; `shufpd` is `TF_SHUF2(ctx->xmm[d], s, n & 1, 2 + ((n >> 1) & 1))`. The helpers `x_shufps` and `x_shufpd` of the family's header are then unused: leave the header as it is."),
 "sse_unpck": ("unpcklps, unpckhps, unpcklpd, unpckhpd",
   "`unpcklps` is `TF_SHUF4(ctx->xmm[d], s, 0, 4, 1, 5)`, `unpckhps` `TF_SHUF4(ctx->xmm[d], s, 2, 6, 3, 7)`, `unpcklpd` `TF_SHUF2(ctx->xmm[d], s, 0, 2)`, `unpckhpd` `TF_SHUF2(ctx->xmm[d], s, 1, 3)`. Check each against the family's present lane code before you replace it: if the present code says something else for a form, keep the present meaning and say so."),
 "sse_movlh": ("movlhps, movhlps",
   "`movlhps xmm_d, xmm_s` (the low half of the source becomes the high half of the destination) is `TF_SHUF2(ctx->xmm[d], s, 0, 2)`; `movhlps xmm_d, xmm_s` (the high half of the source becomes the low half of the destination) is `TF_SHUF2(ctx->xmm[d], s, 3, 1)`. Check each against the family's present lane code first; the present code is the meaning."),
 "sse_movdup": ("movsldup, movshdup, movddup",
   "`movsldup` is `TF_SHUF4(s, s, 0, 0, 2, 2)`, `movshdup` `TF_SHUF4(s, s, 1, 1, 3, 3)` of the SOURCE operand; `movddup` duplicates the low 64 bits of its source (a register or 8 bytes of memory): keep its present code unless the source is a register, then `TF_SHUF2(s, s, 0, 0)`. Check each against the present code first."),
}
TEMPLATE = '''# Task neon-{fam}: the packed instructions of family `{fam}` as one host vector instruction each

You change only this family's files: `lifter/insns/{fam}.py`, and, only if needed,
`harness/insns/{fam}.h` and `tests/vectors/{fam}.py`. Nothing else. Do not read `game/` or `lifted/`.

**Why.** The game is x86-64 code translated to C. A packed SSE operation ({mn}) is translated
today lane by lane, four scalar operations through memory, which is slow. The shared header now
has helpers that do the whole register with one host (arm64) vector instruction, and one family
is already rewritten with them as the pattern.

## Read first, in this order

1. `harness/runtime.h`: the comment block that begins `THE PACKED FORMS` and the one after it that
   begins `Lane shuffles of whole registers` (`grep -n "THE PACKED FORMS\\|Lane shuffles" harness/runtime.h`,
   then `sed -n` about 60 lines from the first): `tf_fadd_ps(a, b)`, `tf_fsub_ps`, `tf_fmul_ps`,
   `tf_fdiv_ps`, `tf_fmin_ps`, `tf_fmax_ps` and their `_pd` forms take and return whole `Xmm`
   values; `tf_fsqrt_ps(a)`, `tf_frecpe_ps(a)`, `tf_frsqrte_ps(a)` take one; `TF_SHUF4(A, B, i0, i1, i2, i3)`
   and `TF_SHUF2(A, B, i0, i1)` pick lanes (an index below 4, or below 2, is a lane of A; the
   rest are lanes of B).
2. The pattern, already rewritten: `lifter/insns/sse_addsub_p.py` (all of it, 40 lines). Its
   translator is now two lines: the source read whole into a local (`tr.xval(1)`), then one
   call on whole registers.
3. Your family: `lifter/insns/{fam}.py` (all of it) and its header if it has one.

## What to do

{what}

Keep everything else of the translator as it is: the registered mnemonics and encodings, the
checks of operands, `Unsupported` for forms it refuses, and above all **the source read into a
local before the destination is written** (`"Xmm s = %s;" % tr.xval(1)`: the source may be the
destination register, and a memory source must fault before anything is written). Update the
file's doc comment to say what the translation now is. Do not change what any instruction
means: if the present lane code and the sentence above disagree for some form, the present
code is right; keep that form as it is and say so in your reply.

## Check (both must pass; the first takes three to six minutes, pipe it as shown)

    make family-check FAMILY={fam} 2>&1 | tail -15      # the last line must say result=PASS
    make test 2>&1 | tail -3

`make family-check` runs the family's instructions against results recorded from a real x86
processor, lane by lane, under seven float modes: it is the judge. If it fails for one
mnemonic, put that one back as it was (leave the others rewritten), run it again, and say
which in your reply. Commit your file(s) with the message
`{fam}: packed forms as one host vector instruction` and reply in at most six lines with the
`FAMILY-CHECK` line.
'''
def main():
    fams = sys.argv[1:] or list(ROWS)
    for fam in fams:
        mn, what = ROWS[fam]
        open("swarm/briefs/neon-%s.md" % fam, "w").write(TEMPLATE.format(fam=fam, mn=mn, what=what))
        u = {"id": "neon-" + fam, "title": "Packed instructions of %s as one host vector instruction each (%s)" % (fam, mn.split(" (")[0]),
             "kind": "insn-task", "issue": 55, "model": "haiku", "priority": 6, "needs": [],
             "check": ["make family-check FAMILY=%s" % fam, "make test"], "state": "todo", "attempts": 0, "history": []}
        with open("swarm/units/neon-%s.json" % fam, "w") as f:
            json.dump(u, f, indent=2, sort_keys=True); f.write("\n")
        print("neon-" + fam)
if __name__ == "__main__":
    main()
