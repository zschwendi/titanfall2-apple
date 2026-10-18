"""SSE scalar float subtraction: subss, subsd.  The addss/addsd pattern with `-` for `+`.

Subtraction is not commutative, so it is a plain C `-` on lane 0 (the destination first) and needs
no tf_ helper: docs/findings/11-float-mode.md measured `d - s` exact in the guest float mode for
both widths (16 of 16 configs, row "addss/sd/ps/pd, sub, mul, div", "Exact C" column).  The source
is read into a local before the destination is written, because source and destination may be the
same register.  Upper lanes are untouched.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


@F.insn("subss", enc(0x0F5C, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("subsd", enc(0x0F5C, mand=0xF2), vector=True, fp="f64 scalar")
def t_sub_scalar(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    bits, lane, ctype = (32, "f", "float") if tr.name == "subss" else (64, "pd", "double")
    return ["%s s = %s;" % (ctype, tr.fval(1, bits)),              # the source is read before anything is written
            "ctx->xmm[%d].%s[0] = ctx->xmm[%d].%s[0] - s;" % (d, lane, d, lane)]
