"""Hand-written test vectors of the sse_sqrt_p family: sqrtps, sqrtpd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

A 16-byte memory operand must be 16-byte aligned (#GP otherwise).  The harness places a
register-relative operand itself: aligned in four states of five, misaligned in the fifth.  A
RIP-relative operand lies where the encoding puts it, so both kinds are written out here.
"""

VECTORS = [
    (0x1000, "0f51ca", "sqrtps xmm1, xmm2"),
    (0x1010, "450f51cc", "sqrtps xmm9, xmm12"),
    (0x1020, "0f51db", "sqrtps xmm3, xmm3"),
    (0x1030, "0f510b", "sqrtps xmm1, xmmword ptr [rbx]"),
    (0x1040, "440f515310", "sqrtps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "0f51548820", "sqrtps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "0f510dd96f0000", "sqrtps xmm1, xmmword ptr [rip + 0x6fd9]"),      # the operand is at 0x8040: aligned
    (0x1070, "0f510dd16f0000", "sqrtps xmm1, xmmword ptr [rip + 0x6fd1]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x1080, "0f515c2420", "sqrtps xmm3, xmmword ptr [rsp + 0x20]"),
    (0x1090, "660f51ca", "sqrtpd xmm1, xmm2"),
    (0x10a0, "66450f51cc", "sqrtpd xmm9, xmm12"),
    (0x10b0, "660f51db", "sqrtpd xmm3, xmm3"),
    (0x10c0, "660f510b", "sqrtpd xmm1, xmmword ptr [rbx]"),
    (0x10d0, "66440f515310", "sqrtpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x10e0, "660f51548820", "sqrtpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x10f0, "660f510d486f0000", "sqrtpd xmm1, xmmword ptr [rip + 0x6f48]"),    # the operand is at 0x8040: aligned
    (0x1100, "660f510d196f0000", "sqrtpd xmm1, xmmword ptr [rip + 0x6f19]"),    # the operand is at 0x8021: not aligned, #GP in every state
    (0x1110, "660f515c2420", "sqrtpd xmm3, xmmword ptr [rsp + 0x20]"),
]
