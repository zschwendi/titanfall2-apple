"""Hand-written test vectors of the sse_pmuldq family: pmuludq and pmaddubsw, each with the
operand forms of the game: xmm,xmm (with a register with itself, and with register 8 or above),
xmm,m128 (register-relative, with a displacement and with an index) and xmm,m128 RIP-relative
(the data at 0x8040).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660ff4ca", "pmuludq xmm1, xmm2"),
    (0x1010, "66450ff4cc", "pmuludq xmm9, xmm12"),
    (0x1020, "660ff4db", "pmuludq xmm3, xmm3"),
    (0x1030, "660ff40b", "pmuludq xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440ff45310", "pmuludq xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660ff4548820", "pmuludq xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660ff40dd86f0000", "pmuludq xmm1, xmmword ptr [rip + 0x6fd8]"),
    (0x1070, "660f3804ca", "pmaddubsw xmm1, xmm2"),
    (0x1080, "66450f3804cc", "pmaddubsw xmm9, xmm12"),
    (0x1090, "660f3804db", "pmaddubsw xmm3, xmm3"),
    (0x10a0, "660f38040b", "pmaddubsw xmm1, xmmword ptr [rbx]"),
    (0x10b0, "66440f38045310", "pmaddubsw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x10c0, "660f3804548820", "pmaddubsw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "660f38040d676f0000", "pmaddubsw xmm1, xmmword ptr [rip + 0x6f67]"),
]
