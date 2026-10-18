"""Hand-written test vectors of the sse_cvtdq2p family: cvtdq2ps, cvtdq2pd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

A 16-byte memory operand must be 16-byte aligned (#GP otherwise).  The harness places a
register-relative operand itself: aligned in four states of five, misaligned in the fifth.  A
RIP-relative operand lies where the encoding puts it, so both kinds are written out here.  The
8-byte memory operand of cvtdq2pd has no alignment rule.
"""

VECTORS = [
    (0x1000, "0f5bca", "cvtdq2ps xmm1, xmm2"),
    (0x1010, "450f5bcc", "cvtdq2ps xmm9, xmm12"),
    (0x1020, "0f5bdb", "cvtdq2ps xmm3, xmm3"),
    (0x1030, "0f5b0b", "cvtdq2ps xmm1, xmmword ptr [rbx]"),
    (0x1040, "440f5b5310", "cvtdq2ps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "0f5b548820", "cvtdq2ps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "0f5b0dd96f0000", "cvtdq2ps xmm1, xmmword ptr [rip + 0x6fd9]"),      # the operand is at 0x8040: aligned
    (0x1070, "f30fe6ca", "cvtdq2pd xmm1, xmm2"),
    (0x1080, "f3450fe6cc", "cvtdq2pd xmm9, xmm12"),
    (0x1090, "f30fe6db", "cvtdq2pd xmm3, xmm3"),
    (0x10a0, "f30fe60b", "cvtdq2pd xmm1, qword ptr [rbx]"),
    (0x10b0, "f3440fe65310", "cvtdq2pd xmm10, qword ptr [rbx + 0x10]"),
    (0x10c0, "f30fe6548820", "cvtdq2pd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "f30fe60d686f0000", "cvtdq2pd xmm1, qword ptr [rip + 0x6f68]"),      # the operand is at 0x8040
    (0x10e0, "0f5b542420", "cvtdq2ps xmm2, xmmword ptr [rsp + 0x20]"),
    (0x10f0, "f30fe65c2420", "cvtdq2pd xmm3, qword ptr [rsp + 0x20]"),
    (0x1100, "0f5b0d416f0000", "cvtdq2ps xmm1, xmmword ptr [rip + 0x6f41]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x1110, "0f5b0d0a6f0000", "cvtdq2ps xmm1, xmmword ptr [rip + 0x6f0a]"),      # the operand is at 0x8021: not aligned, #GP in every state
]
