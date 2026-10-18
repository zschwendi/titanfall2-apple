"""Hand-written test vectors of the sse_movdup family: movddup, movshdup and movsldup, one line per
operand form the game has: xmm, xmm; a register with itself; the high registers (xmm9, xmm12); a
memory operand through rbx, with a displacement, and through a scaled index; and RIP-relative data
at 0x8040 (aligned). The movddup memory operand is qword, the others xmmword.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f20f12ca", "movddup xmm1, xmm2"),
    (0x1010, "f2450f12cc", "movddup xmm9, xmm12"),
    (0x1020, "f20f12db", "movddup xmm3, xmm3"),
    (0x1030, "f20f120b", "movddup xmm1, qword ptr [rbx]"),
    (0x1040, "f2440f125310", "movddup xmm10, qword ptr [rbx + 0x10]"),
    (0x1050, "f20f12548820", "movddup xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f20f120dd86f0000", "movddup xmm1, qword ptr [rip + 0x6fd8]"),
    (0x1100, "f30f16ca", "movshdup xmm1, xmm2"),
    (0x1110, "f3450f16cc", "movshdup xmm9, xmm12"),
    (0x1120, "f30f16db", "movshdup xmm3, xmm3"),
    (0x1130, "f30f160b", "movshdup xmm1, xmmword ptr [rbx]"),
    (0x1140, "f3440f165310", "movshdup xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "f30f16548820", "movshdup xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "f30f160dd86e0000", "movshdup xmm1, xmmword ptr [rip + 0x6ed8]"),
    (0x1200, "f30f12ca", "movsldup xmm1, xmm2"),
    (0x1210, "f3450f12cc", "movsldup xmm9, xmm12"),
    (0x1220, "f30f12db", "movsldup xmm3, xmm3"),
    (0x1230, "f30f120b", "movsldup xmm1, xmmword ptr [rbx]"),
    (0x1240, "f3440f125310", "movsldup xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "f30f12548820", "movsldup xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "f30f120dd86d0000", "movsldup xmm1, xmmword ptr [rip + 0x6dd8]"),
]
