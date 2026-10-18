"""Non-temporal store of a general register: movnti."""

from ..translate import Family, Unsupported, enc

F = Family(__name__)                                   # no C helpers: no header


@F.insn("movnti", enc(0x0FC3, mand=0))
def t_movnti(tr, flow):
    tr.need_ops(2)
    b = tr.bits(0)
    if not (tr.is_mem(0) and b in (32, 64)):
        raise Unsupported("movnti form: only m32,r32 and m64,r64")
    return [tr.wr(0, tr.val(1, b), b)]
