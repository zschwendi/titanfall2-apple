"""Hand-written test vectors of the sse_padd family: paddb, paddw, paddd and paddq, each with the
register forms (xmm,xmm, the same register twice, and xmm8 and above) and the memory forms
(xmmword ptr [rbx], [rbx + disp8], the scaled index, and RIP-relative to the data at 0x8040).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1100, "660ffcca", "paddb xmm1, xmm2"),
    (0x1110, "66450ffccc", "paddb xmm9, xmm12"),
    (0x1120, "660ffcdb", "paddb xmm3, xmm3"),
    (0x1130, "660ffc0b", "paddb xmm1, xmmword ptr [rbx]"),
    (0x1140, "66440ffc5310", "paddb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "660ffc548820", "paddb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "660ffc0dd86e0000", "paddb xmm1, xmmword ptr [rip + 0x6ed8]"),
    (0x1200, "660ffdca", "paddw xmm1, xmm2"),
    (0x1210, "66450ffdcc", "paddw xmm9, xmm12"),
    (0x1220, "660ffddb", "paddw xmm3, xmm3"),
    (0x1230, "660ffd0b", "paddw xmm1, xmmword ptr [rbx]"),
    (0x1240, "66440ffd5310", "paddw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "660ffd548820", "paddw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "660ffd0dd86d0000", "paddw xmm1, xmmword ptr [rip + 0x6dd8]"),
    (0x1300, "660ffeca", "paddd xmm1, xmm2"),
    (0x1310, "66450ffecc", "paddd xmm9, xmm12"),
    (0x1320, "660ffedb", "paddd xmm3, xmm3"),
    (0x1330, "660ffe0b", "paddd xmm1, xmmword ptr [rbx]"),
    (0x1340, "66440ffe5310", "paddd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1350, "660ffe548820", "paddd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "660ffe0dd86c0000", "paddd xmm1, xmmword ptr [rip + 0x6cd8]"),
    (0x1400, "660fd4ca", "paddq xmm1, xmm2"),
    (0x1410, "66450fd4cc", "paddq xmm9, xmm12"),
    (0x1420, "660fd4db", "paddq xmm3, xmm3"),
    (0x1430, "660fd40b", "paddq xmm1, xmmword ptr [rbx]"),
    (0x1440, "66440fd45310", "paddq xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1450, "660fd4548820", "paddq xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1460, "660fd40dd86b0000", "paddq xmm1, xmmword ptr [rip + 0x6bd8]"),
]
