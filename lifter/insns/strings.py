"""String instructions: movs, cmps, stos, lods, scas on bytes, words, doublewords and quadwords,
alone or repeated by an F3/F2 prefix.

Names: the size letter is b, w, l or q.  The doubleword forms are movsl, cmpsl, stosl, lodsl
and scasl here, not movsd, ... as the decoder prints them, because MOVSD and CMPSD are also
SSE2 instructions (see mnemonic_of in lifter/translate.py).
"""

from capstone import x86_const as X

from ..translate import GPR, STRING_OPCODES, Family, Unsupported, enc

F = Family(__name__)

SIZE = {"b": 1, "w": 2, "l": 4, "q": 8}


def t_string(tr, flow):
    ins, n = tr.ins, tr.name
    # The string instructions are the one-byte opcodes A4..A7 and AA..AF and nothing
    # else; the same names are used for SSE instructions (movsd, cmpsd) and, by the
    # decoder, for SSE compares with a predicate.
    if tr.enc.key not in STRING_OPCODES or len(tr.ops) != 2 \
            or any(op.type == X.X86_OP_REG and op.reg not in GPR for op in tr.ops):
        raise Unsupported("%s is not a string instruction" % ins.mnemonic)
    if ins.addr_size != 8:
        raise Unsupported(n + " with 32-bit address size")
    for op in tr.ops:
        if op.type == X.X86_OP_MEM and op.mem.segment in (X.X86_REG_FS, X.X86_REG_GS):
            raise Unsupported(n + " with segment override")
    kind, sz = n[:4], SIZE[n[4]]
    b = sz * 8
    body = {
        "movs": "ST%d(ctx->r[7], LD%d(ctx->r[6])); ctx->r[6] += d; ctx->r[7] += d;" % (b, b),
        "stos": "ST%d(ctx->r[7], R%d(0)); ctx->r[7] += d;" % (b, b),
        "lods": "W%d(0, LD%d(ctx->r[6])); ctx->r[6] += d;" % (b, b),
        "scas": "(void)x_sub%d(ctx, R%d(0), LD%d(ctx->r[7])); ctx->r[7] += d;" % (b, b, b),
        "cmps": "(void)x_sub%d(ctx, LD%d(ctx->r[6]), LD%d(ctx->r[7])); ctx->r[6] += d; ctx->r[7] += d;" % (b, b, b),
    }[kind]
    lines = ["uint64_t d = ctx->df ? (uint64_t)-%d : (uint64_t)%d;" % (sz, sz)]
    if not tr.rep:
        lines.append(body)
    elif kind in ("scas", "cmps"):
        stop = "!ctx->zf" if tr.rep == 0xF3 else "ctx->zf"     # repe / repne
        lines.append("while (ctx->r[1] != 0) { %s ctx->r[1]--; if (%s) break; }" % (body, stop))
    else:
        lines.append("while (ctx->r[1] != 0) { %s ctx->r[1]--; }" % body)
    return lines


for _kind, _op in (("movs", 0xA4), ("cmps", 0xA6), ("stos", 0xAA), ("lods", 0xAC), ("scas", 0xAE)):
    for _s in SIZE:
        F.register(_kind + _s, t_string, enc(_op if _s == "b" else _op + 1))
