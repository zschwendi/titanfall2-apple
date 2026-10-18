"""Hand-written test vectors of the sse_addsub_p family: addps, addpd, subps, subpd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

A 16-byte memory operand must be 16-byte aligned (#GP otherwise).  The harness places a
register-relative operand itself: aligned in four states of five, misaligned in the fifth.  A
RIP-relative operand lies where the encoding puts it, so both kinds are written out here.
"""

VECTORS = [
    (0x1000, "0f58ca", "addps xmm1, xmm2"),
    (0x1010, "450f58cc", "addps xmm9, xmm12"),
    (0x1020, "0f58db", "addps xmm3, xmm3"),
    (0x1030, "0f580b", "addps xmm1, xmmword ptr [rbx]"),
    (0x1040, "440f585310", "addps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "0f58548820", "addps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "0f580dd96f0000", "addps xmm1, xmmword ptr [rip + 0x6fd9]"),      # the operand is at 0x8040: aligned
    (0x1070, "660f58ca", "addpd xmm1, xmm2"),
    (0x1080, "66450f58cc", "addpd xmm9, xmm12"),
    (0x1090, "660f58db", "addpd xmm3, xmm3"),
    (0x10a0, "660f580b", "addpd xmm1, xmmword ptr [rbx]"),
    (0x10b0, "66440f585310", "addpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x10c0, "660f58548820", "addpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "660f580d686f0000", "addpd xmm1, xmmword ptr [rip + 0x6f68]"),      # the operand is at 0x8040: aligned
    (0x10e0, "0f5cca", "subps xmm1, xmm2"),
    (0x10f0, "450f5ccc", "subps xmm9, xmm12"),
    (0x1100, "0f5cdb", "subps xmm3, xmm3"),
    (0x1110, "0f5c0b", "subps xmm1, xmmword ptr [rbx]"),
    (0x1120, "440f5c5310", "subps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1130, "0f5c548820", "subps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1140, "0f5c0df96e0000", "subps xmm1, xmmword ptr [rip + 0x6ef9]"),      # the operand is at 0x8040: aligned
    (0x1150, "660f5cca", "subpd xmm1, xmm2"),
    (0x1160, "66450f5ccc", "subpd xmm9, xmm12"),
    (0x1170, "660f5cdb", "subpd xmm3, xmm3"),
    (0x1180, "660f5c0b", "subpd xmm1, xmmword ptr [rbx]"),
    (0x1190, "66440f5c5310", "subpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x11a0, "660f5c548820", "subpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x11b0, "660f5c0d886e0000", "subpd xmm1, xmmword ptr [rip + 0x6e88]"),      # the operand is at 0x8040: aligned
    (0x11c0, "0f585c2420", "addps xmm3, xmmword ptr [rsp + 0x20]"),
    (0x11d0, "660f5c5c2420", "subpd xmm3, xmmword ptr [rsp + 0x20]"),
    (0x11e0, "0f580d616e0000", "addps xmm1, xmmword ptr [rip + 0x6e61]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x11f0, "660f5c0d296e0000", "subpd xmm1, xmmword ptr [rip + 0x6e29]"),      # the operand is at 0x8021: not aligned, #GP in every state
]
