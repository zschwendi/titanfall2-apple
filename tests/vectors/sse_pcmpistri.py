"""Hand-written test vectors of the sse_pcmpistri family: pcmpistri with each of the five control
bytes the game uses (0x0c, 0x0d, 0x15, 0x40, 0x41) in every operand form (two registers, a
register numbered 8 or above, a register with itself, and the m128 forms, which need no
alignment), and control bytes that must be refused: signed elements (0x0e), equal each (0x08),
the masked polarities (0x2c, 0x3c), the other combinations nearest to the translated ones
(0x00, 0x01, 0x04, 0x05, 0x14, 0x1c, 0x4c, 0x4d, 0x50) and bit 7 (0x8c).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660f3a63ca0c", "pcmpistri xmm1, xmm2, 0xc"),
    (0x1010, "660f3a63ca0d", "pcmpistri xmm1, xmm2, 0xd"),
    (0x1020, "660f3a63ca15", "pcmpistri xmm1, xmm2, 0x15"),
    (0x1030, "660f3a63ca40", "pcmpistri xmm1, xmm2, 0x40"),
    (0x1040, "660f3a63ca41", "pcmpistri xmm1, xmm2, 0x41"),
    (0x1050, "66450f3a63cc0c", "pcmpistri xmm9, xmm12, 0xc"),
    (0x1060, "66450f3a63cc0d", "pcmpistri xmm9, xmm12, 0xd"),
    (0x1070, "66450f3a63cc15", "pcmpistri xmm9, xmm12, 0x15"),
    (0x1080, "66450f3a63cc40", "pcmpistri xmm9, xmm12, 0x40"),
    (0x1090, "66450f3a63cc41", "pcmpistri xmm9, xmm12, 0x41"),
    (0x10a0, "660f3a63db0c", "pcmpistri xmm3, xmm3, 0xc"),
    (0x10b0, "660f3a63db0d", "pcmpistri xmm3, xmm3, 0xd"),
    (0x10c0, "660f3a63db15", "pcmpistri xmm3, xmm3, 0x15"),
    (0x10d0, "660f3a63db40", "pcmpistri xmm3, xmm3, 0x40"),
    (0x10e0, "660f3a63db41", "pcmpistri xmm3, xmm3, 0x41"),
    (0x10f0, "660f3a630b0c", "pcmpistri xmm1, xmmword ptr [rbx], 0xc"),
    (0x1100, "660f3a630b0d", "pcmpistri xmm1, xmmword ptr [rbx], 0xd"),
    (0x1110, "660f3a630b15", "pcmpistri xmm1, xmmword ptr [rbx], 0x15"),
    (0x1120, "660f3a630b40", "pcmpistri xmm1, xmmword ptr [rbx], 0x40"),
    (0x1130, "660f3a630b41", "pcmpistri xmm1, xmmword ptr [rbx], 0x41"),
    (0x1140, "660f3a635488200c", "pcmpistri xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0xc"),
    (0x1150, "660f3a635488200d", "pcmpistri xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0xd"),
    (0x1160, "660f3a6354882015", "pcmpistri xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0x15"),
    (0x1170, "660f3a6354882040", "pcmpistri xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0x40"),
    (0x1180, "660f3a6354882041", "pcmpistri xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0x41"),
    (0x1190, "660f3a630da66e00000c", "pcmpistri xmm1, xmmword ptr [rip + 0x6ea6], 0xc"),
    (0x11a0, "660f3a630d966e00000d", "pcmpistri xmm1, xmmword ptr [rip + 0x6e96], 0xd"),
    (0x11b0, "660f3a630d866e000015", "pcmpistri xmm1, xmmword ptr [rip + 0x6e86], 0x15"),
    (0x11c0, "660f3a630d766e000040", "pcmpistri xmm1, xmmword ptr [rip + 0x6e76], 0x40"),
    (0x11d0, "660f3a630d666e000041", "pcmpistri xmm1, xmmword ptr [rip + 0x6e66], 0x41"),
]

REFUSED = [
    (0x2000, "660f3a63ca00", "pcmpistri xmm1, xmm2, 0", "pcmpistri control byte"),
    (0x2010, "660f3a63ca01", "pcmpistri xmm1, xmm2, 1", "pcmpistri control byte"),
    (0x2020, "660f3a63ca04", "pcmpistri xmm1, xmm2, 4", "pcmpistri control byte"),
    (0x2030, "660f3a63ca05", "pcmpistri xmm1, xmm2, 5", "pcmpistri control byte"),
    (0x2040, "660f3a63ca08", "pcmpistri xmm1, xmm2, 8", "pcmpistri control byte"),
    (0x2050, "660f3a63ca0e", "pcmpistri xmm1, xmm2, 0xe", "pcmpistri control byte"),
    (0x2060, "660f3a63ca14", "pcmpistri xmm1, xmm2, 0x14", "pcmpistri control byte"),
    (0x2070, "660f3a63ca1c", "pcmpistri xmm1, xmm2, 0x1c", "pcmpistri control byte"),
    (0x2080, "660f3a63ca2c", "pcmpistri xmm1, xmm2, 0x2c", "pcmpistri control byte"),
    (0x2090, "660f3a63ca3c", "pcmpistri xmm1, xmm2, 0x3c", "pcmpistri control byte"),
    (0x20a0, "660f3a63ca4c", "pcmpistri xmm1, xmm2, 0x4c", "pcmpistri control byte"),
    (0x20b0, "660f3a63ca4d", "pcmpistri xmm1, xmm2, 0x4d", "pcmpistri control byte"),
    (0x20c0, "660f3a63ca50", "pcmpistri xmm1, xmm2, 0x50", "pcmpistri control byte"),
    (0x20d0, "660f3a63ca8c", "pcmpistri xmm1, xmm2, 0x8c", "pcmpistri control byte"),
]
