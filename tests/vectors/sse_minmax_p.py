"""Hand-written test vectors of the sse_minmax_p family: minps, maxps, minpd, maxpd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

A 16-byte memory operand must be 16-byte aligned (#GP otherwise).  The harness places a
register-relative operand itself: aligned in four states of five, misaligned in the fifth.  A
RIP-relative operand lies where the encoding puts it, so both kinds are written out here.
"""

VECTORS = [
    (0x1000, "0f5dca", "minps xmm1, xmm2"),
    (0x1010, "450f5dcc", "minps xmm9, xmm12"),
    (0x1020, "0f5ddb", "minps xmm3, xmm3"),
    (0x1030, "0f5d0b", "minps xmm1, xmmword ptr [rbx]"),
    (0x1040, "440f5d5310", "minps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "0f5d548820", "minps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "0f5d0dd96f0000", "minps xmm1, xmmword ptr [rip + 0x6fd9]"),      # the operand is at 0x8040: aligned
    (0x1070, "0f5fca", "maxps xmm1, xmm2"),
    (0x1080, "450f5fcc", "maxps xmm9, xmm12"),
    (0x1090, "0f5fdb", "maxps xmm3, xmm3"),
    (0x10a0, "0f5f0b", "maxps xmm1, xmmword ptr [rbx]"),
    (0x10b0, "440f5f5310", "maxps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x10c0, "0f5f548820", "maxps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "0f5f0d696f0000", "maxps xmm1, xmmword ptr [rip + 0x6f69]"),      # the operand is at 0x8040: aligned
    (0x10e0, "660f5dca", "minpd xmm1, xmm2"),
    (0x10f0, "66450f5dcc", "minpd xmm9, xmm12"),
    (0x1100, "660f5ddb", "minpd xmm3, xmm3"),
    (0x1110, "660f5d0b", "minpd xmm1, xmmword ptr [rbx]"),
    (0x1120, "66440f5d5310", "minpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1130, "660f5d548820", "minpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1140, "660f5d0df86e0000", "minpd xmm1, xmmword ptr [rip + 0x6ef8]"),      # the operand is at 0x8040: aligned
    (0x1150, "660f5fca", "maxpd xmm1, xmm2"),
    (0x1160, "66450f5fcc", "maxpd xmm9, xmm12"),
    (0x1170, "660f5fdb", "maxpd xmm3, xmm3"),
    (0x1180, "660f5f0b", "maxpd xmm1, xmmword ptr [rbx]"),
    (0x1190, "66440f5f5310", "maxpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x11a0, "660f5f548820", "maxpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x11b0, "660f5f0d886e0000", "maxpd xmm1, xmmword ptr [rip + 0x6e88]"),      # the operand is at 0x8040: aligned
    (0x11c0, "660f5d0d806e0000", "minpd xmm1, xmmword ptr [rip + 0x6e80]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x11d0, "660f5f0d496e0000", "maxpd xmm1, xmmword ptr [rip + 0x6e49]"),      # the operand is at 0x8021: not aligned, #GP in every state
]
