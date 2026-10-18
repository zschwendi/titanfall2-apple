"""Hand-written test vectors of the sse_pminsd family: pminsd, the minimum of signed doublewords,
in the three operand forms of the game: xmm,xmm (with itself, and with registers 8 and above),
xmm,m128 (with a displacement and with an index), and xmm,m128 RIP-relative (its data at 0x8040,
which is aligned).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660f3839ca", "pminsd xmm1, xmm2"),
    (0x1010, "66450f3839cc", "pminsd xmm9, xmm12"),
    (0x1020, "660f3839db", "pminsd xmm3, xmm3"),
    (0x1030, "660f38390b", "pminsd xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440f38395310", "pminsd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660f3839548820", "pminsd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660f38390dd76f0000", "pminsd xmm1, xmmword ptr [rip + 0x6fd7]"),
]
