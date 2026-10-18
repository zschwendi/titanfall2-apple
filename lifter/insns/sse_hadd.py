"""SSE3 packed single add/subtract and horizontal add: addsubps, haddps.

    addsubps   r = {d0 - s0, d1 + s1, d2 - s2, d3 + s3}
    haddps     r = {d0 + d1, d2 + d3, s0 + s1, s2 + s3}

(d = the destination before the instruction, s = the source.)  Declared float with
fp="f32 packed": the harness gives the lanes float-aware operands, runs every case under the
seven MXCSR settings and takes the expected result from the real CPU.

The formulation is the one measured on a Core i5-7600K and written into the results table of
docs/findings/11-float-mode.md (row "addsubps, haddps"; harness/hadd_sweep.py is the
measurement and keeps comparing this translation with the CPU's answers):

  * which lanes, and which of them are subtracted, as above (the Intel manual's definition);
  * where both inputs of a result lane are NaNs the result is the FIRST one, quieted, under
    every MXCSR setting: for addsubps the destination's lane, as for addps and subps; for haddps
    the LOWER lane of the two (d0 in d0 + d1, s2 in s2 + s3), although the manual writes the
    sum the other way round ("SRC1[63:32] + SRC1[31:0]").

So each lane is the scalar operation finding 11 measured exact in the guest float mode: an
addition through tf_fadd32 of harness/runtime.h, which keeps the operand order (the compiler may
swap the operands of a C `+`), a subtraction as plain C.

Both operands are copied before anything is written: the source may be the destination, and
haddps reads destination lanes 2 and 3 after lane 0 would have been written.  The source may be 16
bytes of memory, which x86 requires to be 16-byte aligned: tr.xval(1) emits the #GP before the
load.  Never read such an operand with LDX(tr.ea(1)) yourself.
"""

from ..translate import Family, Unsupported, enc

F = Family(__name__)

# result lane -> (operation, first input, second input); an input is (operand, lane), operand
# "a" = the destination as it was, "s" = the source
LANES = {
    "addsubps": [("sub" if j % 2 == 0 else "add", ("a", j), ("s", j)) for j in range(4)],
    "haddps": [("add", ("a", 0), ("a", 1)), ("add", ("a", 2), ("a", 3)),
               ("add", ("s", 0), ("s", 1)), ("add", ("s", 2), ("s", 3))],
}


@F.insn("addsubps", enc(0x0FD0, mand=0xF2), vector=True, fp="f32 packed")
@F.insn("haddps", enc(0x0F7C, mand=0xF2), vector=True, fp="f32 packed")
def t_hadd(tr, flow):
    tr.need_ops(2)
    d = tr.xr(0)
    if d is None:
        raise Unsupported(tr.name + " destination")
    out = ["Xmm s = %s;" % tr.xval(1),                  # an xmm register or an aligned m128, read whole first
           "Xmm a = ctx->xmm[%d];" % d]
    for j, (op, (r1, l1), (r2, l2)) in enumerate(LANES[tr.name]):
        x, y = "%s.f[%d]" % (r1, l1), "%s.f[%d]" % (r2, l2)
        out.append("ctx->xmm[%d].f[%d] = %s;" % (d, j, "tf_fadd32(%s, %s)" % (x, y) if op == "add" else "%s - %s" % (x, y)))
    return out
