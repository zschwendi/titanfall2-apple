"""Hand-written test vectors of the sse_psadbw family: psadbw, the sum of absolute byte
differences, in every operand form the game has: xmm,xmm (distinct, the same register twice, and
register 8 or above), xmm,m128 (plain, register 8 or above, and with an index), and xmm,m128 RIP-
relative (the data at 0x8040, aligned).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660ff6ca", "psadbw xmm1, xmm2"),
    (0x1010, "66450ff6cc", "psadbw xmm9, xmm12"),
    (0x1020, "660ff6db", "psadbw xmm3, xmm3"),
    (0x1030, "660ff60b", "psadbw xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440ff65310", "psadbw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660ff6548820", "psadbw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660ff60dd86f0000", "psadbw xmm1, xmmword ptr [rip + 0x6fd8]"),
]
