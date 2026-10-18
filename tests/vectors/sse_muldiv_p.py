"""Hand-written test vectors of the sse_muldiv_p family: mulps, mulpd, divps, divpd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

A 16-byte memory operand must be 16-byte aligned (#GP otherwise).  The harness places a
register-relative operand itself: aligned in four states of five, misaligned in the fifth.  A
RIP-relative operand lies where the encoding puts it, so both kinds are written out here.
"""

VECTORS = [
    (0x1000, "0f59ca", "mulps xmm1, xmm2"),
    (0x1010, "450f59cc", "mulps xmm9, xmm12"),
    (0x1020, "0f59db", "mulps xmm3, xmm3"),
    (0x1030, "0f590b", "mulps xmm1, xmmword ptr [rbx]"),
    (0x1040, "440f595310", "mulps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "0f59548820", "mulps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "0f590dd96f0000", "mulps xmm1, xmmword ptr [rip + 0x6fd9]"),      # the operand is at 0x8040: aligned
    (0x1070, "0f5eca", "divps xmm1, xmm2"),
    (0x1080, "450f5ecc", "divps xmm9, xmm12"),
    (0x1090, "0f5edb", "divps xmm3, xmm3"),
    (0x10a0, "0f5e0b", "divps xmm1, xmmword ptr [rbx]"),
    (0x10b0, "440f5e5310", "divps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x10c0, "0f5e548820", "divps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "0f5e0d696f0000", "divps xmm1, xmmword ptr [rip + 0x6f69]"),      # the operand is at 0x8040: aligned
    (0x10e0, "660f59ca", "mulpd xmm1, xmm2"),
    (0x10f0, "66450f59cc", "mulpd xmm9, xmm12"),
    (0x1100, "660f59db", "mulpd xmm3, xmm3"),
    (0x1110, "660f590b", "mulpd xmm1, xmmword ptr [rbx]"),
    (0x1120, "66440f595310", "mulpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1130, "660f59548820", "mulpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1140, "660f590df86e0000", "mulpd xmm1, xmmword ptr [rip + 0x6ef8]"),      # the operand is at 0x8040: aligned
    (0x1150, "660f5eca", "divpd xmm1, xmm2"),
    (0x1160, "66450f5ecc", "divpd xmm9, xmm12"),
    (0x1170, "660f5edb", "divpd xmm3, xmm3"),
    (0x1180, "660f5e0b", "divpd xmm1, xmmword ptr [rbx]"),
    (0x1190, "66440f5e5310", "divpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x11a0, "660f5e548820", "divpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x11b0, "660f5e0d886e0000", "divpd xmm1, xmmword ptr [rip + 0x6e88]"),      # the operand is at 0x8040: aligned
    (0x11c0, "0f590d816e0000", "mulps xmm1, xmmword ptr [rip + 0x6e81]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x11d0, "0f5e0d4a6e0000", "divps xmm1, xmmword ptr [rip + 0x6e4a]"),      # the operand is at 0x8021: not aligned, #GP in every state
    (0x11e0, "660f590d606e0000", "mulpd xmm1, xmmword ptr [rip + 0x6e60]"),    # the operand is at 0x8048: not aligned, #GP in every state
    (0x11f0, "660f5e0d296e0000", "divpd xmm1, xmmword ptr [rip + 0x6e29]"),    # the operand is at 0x8021: not aligned, #GP in every state
]
