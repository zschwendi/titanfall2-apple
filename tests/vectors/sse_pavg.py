"""Hand-written test vectors of the sse_pavg family: pavgb and pavgw, with every operand form the
game has: xmm with xmm (a register with itself, and registers numbered 8 and above), xmm with a
16-byte memory operand (based, indexed, and RIP-relative; the RIP-relative one reads the aligned
data at 0x8040).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1100, "660fe0ca", "pavgb xmm1, xmm2"),
    (0x1110, "66450fe0cc", "pavgb xmm9, xmm12"),
    (0x1120, "660fe0db", "pavgb xmm3, xmm3"),
    (0x1130, "660fe00b", "pavgb xmm1, xmmword ptr [rbx]"),
    (0x1140, "66440fe05310", "pavgb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "660fe0548820", "pavgb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "660fe00dd86e0000", "pavgb xmm1, xmmword ptr [rip + 0x6ed8]"),
    (0x1200, "660fe3ca", "pavgw xmm1, xmm2"),
    (0x1210, "66450fe3cc", "pavgw xmm9, xmm12"),
    (0x1220, "660fe3db", "pavgw xmm3, xmm3"),
    (0x1230, "660fe30b", "pavgw xmm1, xmmword ptr [rbx]"),
    (0x1240, "66440fe35310", "pavgw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "660fe3548820", "pavgw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "660fe30dd86d0000", "pavgw xmm1, xmmword ptr [rip + 0x6dd8]"),
]
