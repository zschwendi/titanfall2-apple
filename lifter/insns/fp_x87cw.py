"""The x87 control word: fnstcw, fldcw, fnclex (D9 /7, D9 /5, DB E2).

This is all the x87 the game's real code has (docs/findings/15-x87.md, summary items 2, 3 and
6): one routine that saves the control word, loads another and clears the exception flags;
nothing is ever pushed on the x87 register stack.  So the x87 state of lifted code is one
16-bit field, ctx->fpcw, which starts as 0x027F (TF_FPCW_DEFAULT, a new Windows x64 thread):

    fnstcw m16   stores it
    fldcw m16    loads it: all 16 bits, as the emulator does.  (A real CPU is said to keep the
                 reserved bit 6 set and bits 7 and 13-15 clear whatever is loaded; that could
                 not be measured, the hardware runner refuses x87, and a control word derived
                 from a stored one has those bits that way already.)
    fnclex       clears the exception flags of the x87 status word.  Ctx has no status word and
                 no instruction that could read one is translated, so there is nothing to do.

The control word has no effect on SSE arithmetic, so nothing else reads the field.  Every
other x87 mnemonic stays untranslated (fstcw and fclex are these two after a `wait`, which is
an instruction of its own and not translated).
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)


def _m16(tr):
    tr.need_ops(1)
    if not tr.is_mem(0) or tr.bits(0) != 16:
        raise Unsupported(tr.name + " operand")


@F.insn("fnstcw", enc(0xD9, reg=7, mand=0))
def t_fnstcw(tr, flow):
    _m16(tr)
    return [tr.wr(0, "ctx->fpcw", 16)]


@F.insn("fldcw", enc(0xD9, reg=5, mand=0))
def t_fldcw(tr, flow):
    _m16(tr)
    return ["ctx->fpcw = %s;" % tr.val(0, 16)]


@F.insn("fnclex", enc(0xDB, reg=4, mand=0))
def t_fnclex(tr, flow):
    tr.need_ops(0)
    return []                                          # no x87 status word in Ctx: nothing to clear
