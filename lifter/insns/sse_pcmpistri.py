"""SSE4.2 implicit-length string compare with an index result: pcmpistri (66 0F 3A 63 /r ib).

`pcmpistri xmm1, xmm2/m128, imm8` compares two strings of 16 bytes or 8 words, each ending
before its first zero element, in the way the control byte imm8 selects, and writes an index
to ECX and all six arithmetic flags; neither operand is written.  The control byte has 256
values.  The game uses five (97 instances), and those are the ones translated:

    0x0c  equal ordered, unsigned bytes, least significant index   51   (substring search)
    0x0d  the same on unsigned words                                6
    0x15  ranges, unsigned words, negative polarity, least index    3
    0x40  equal any, unsigned bytes, most significant index        34   (the only one with m128)
    0x41  the same on unsigned words                                3

Every other control byte is refused, with the byte in the message: signed elements, the
"equal each" aggregation and the two masked polarities occur nowhere in the game and are not
written in the C helper either (harness/insns/sse_pcmpistri.h, which has the four steps of
the SDM's definition).  The 16-byte memory operand has no alignment rule (tr.xval knows).
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_pcmpistri.h")

CONTROL = frozenset((0x0C, 0x0D, 0x15, 0x40, 0x41))


@F.insn("pcmpistri", enc(0x0F3A63, mand=0x66), vector=True)
def t_pcmpistri(tr, flow):
    tr.need_ops(3)
    a = tr.xr(0)
    if a is None:
        raise Unsupported("pcmpistri first operand")
    ctl = tr.ops[2].imm & 0xFF
    if ctl not in CONTROL:
        raise Unsupported("pcmpistri control byte 0x%02x" % ctl)
    return ["Xmm b = %s;" % tr.xval(1),
            "x_pcmpistri(ctx, ctx->xmm[%d], b, 0x%02x);" % (a, ctl)]
