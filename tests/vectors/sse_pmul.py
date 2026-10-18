"""Hand-written test vectors of the sse_pmul family: pmullw, pmulhw, pmulhuw (16-bit lanes) and
pmulld (32-bit lanes), each with the xmm forms x,x (a register with itself, and registers 9 and
12), x,m128 (register-relative, indexed, and RIP-relative to the data at 0x8040). REFUSED holds
the MMX forms the game has (no 66 prefix), which these translators must refuse.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660fd5ca", "pmullw xmm1, xmm2"),
    (0x1010, "66450fd5cc", "pmullw xmm9, xmm12"),
    (0x1020, "660fd5db", "pmullw xmm3, xmm3"),
    (0x1030, "660fd50b", "pmullw xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440fd55310", "pmullw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660fd5548820", "pmullw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660fd50dd86f0000", "pmullw xmm1, xmmword ptr [rip + 0x6fd8]"),
    (0x1100, "660fe5ca", "pmulhw xmm1, xmm2"),
    (0x1110, "66450fe5cc", "pmulhw xmm9, xmm12"),
    (0x1120, "660fe5db", "pmulhw xmm3, xmm3"),
    (0x1130, "660fe50b", "pmulhw xmm1, xmmword ptr [rbx]"),
    (0x1140, "66440fe55310", "pmulhw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "660fe5548820", "pmulhw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "660fe50dd86e0000", "pmulhw xmm1, xmmword ptr [rip + 0x6ed8]"),
    (0x1200, "660fe4ca", "pmulhuw xmm1, xmm2"),
    (0x1210, "66450fe4cc", "pmulhuw xmm9, xmm12"),
    (0x1220, "660fe4db", "pmulhuw xmm3, xmm3"),
    (0x1230, "660fe40b", "pmulhuw xmm1, xmmword ptr [rbx]"),
    (0x1240, "66440fe45310", "pmulhuw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "660fe4548820", "pmulhuw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "660fe40dd86d0000", "pmulhuw xmm1, xmmword ptr [rip + 0x6dd8]"),
    (0x1300, "660f3840ca", "pmulld xmm1, xmm2"),
    (0x1310, "66450f3840cc", "pmulld xmm9, xmm12"),
    (0x1320, "660f3840db", "pmulld xmm3, xmm3"),
    (0x1330, "660f38400b", "pmulld xmm1, xmmword ptr [rbx]"),
    (0x1340, "66440f38405310", "pmulld xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1350, "660f3840548820", "pmulld xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "660f38400dd76c0000", "pmulld xmm1, xmmword ptr [rip + 0x6cd7]"),
]

REFUSED = [
    (0x1400, "0fd5c1", "pmullw mm0, mm1", "pmullw"),
    (0x1410, "0fe5c1", "pmulhw mm0, mm1", "pmulhw"),
    (0x1420, "0fe4c1", "pmulhuw mm0, mm1", "pmulhuw"),
    (0x1430, "0fd50b", "pmullw mm1, qword ptr [rbx]", "pmullw"),
]
