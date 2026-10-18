"""Hand-written test vectors of the sse_pcmpeq family: pcmpeqb, pcmpeqw and pcmpeqd, each with
the three operand forms of the game (xmm,xmm; xmm,m128; xmm,m128 RIP-relative at 0x8040), a
register with itself, and a register numbered 8 or above (the form hi).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660f74ca", "pcmpeqb xmm1, xmm2"),
    (0x1010, "66450f74cc", "pcmpeqb xmm9, xmm12"),
    (0x1020, "660f74db", "pcmpeqb xmm3, xmm3"),
    (0x1030, "660f740b", "pcmpeqb xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440f745310", "pcmpeqb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660f74548820", "pcmpeqb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660f740dd86f0000", "pcmpeqb xmm1, xmmword ptr [rip + 0x6fd8]"),
    (0x1100, "660f75ca", "pcmpeqw xmm1, xmm2"),
    (0x1110, "66450f75cc", "pcmpeqw xmm9, xmm12"),
    (0x1120, "660f75db", "pcmpeqw xmm3, xmm3"),
    (0x1130, "660f750b", "pcmpeqw xmm1, xmmword ptr [rbx]"),
    (0x1140, "66440f755310", "pcmpeqw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "660f75548820", "pcmpeqw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "660f750dd86e0000", "pcmpeqw xmm1, xmmword ptr [rip + 0x6ed8]"),
    (0x1200, "660f76ca", "pcmpeqd xmm1, xmm2"),
    (0x1210, "66450f76cc", "pcmpeqd xmm9, xmm12"),
    (0x1220, "660f76db", "pcmpeqd xmm3, xmm3"),
    (0x1230, "660f760b", "pcmpeqd xmm1, xmmword ptr [rbx]"),
    (0x1240, "66440f765310", "pcmpeqd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "660f76548820", "pcmpeqd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "660f760dd86d0000", "pcmpeqd xmm1, xmmword ptr [rip + 0x6dd8]"),
]
