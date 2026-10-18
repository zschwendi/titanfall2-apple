"""Hand-written test vectors of the sse_palignr family: palignr, the byte alignment of two xmm
registers, with the register form (the same register twice, and registers 9 and 12 in the high
half), the memory forms (at [rbx], [rbx + 0x10], a SIB address, and RIP-relative to the aligned
data at 0x8040), and immediates 0, 4, 8, 0xf, 0x10, 0x11, 0x1f, 0x20 and 0xff.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660f3a0fca00", "palignr xmm1, xmm2, 0"),           # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1010, "660f3a0fca04", "palignr xmm1, xmm2, 4"),           # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1020, "660f3a0fca08", "palignr xmm1, xmm2, 8"),           # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1030, "660f3a0fca0f", "palignr xmm1, xmm2, 0xf"),         # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1040, "660f3a0fca10", "palignr xmm1, xmm2, 0x10"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1050, "660f3a0fca11", "palignr xmm1, xmm2, 0x11"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1060, "660f3a0fca1f", "palignr xmm1, xmm2, 0x1f"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1070, "660f3a0fca20", "palignr xmm1, xmm2, 0x20"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1080, "660f3a0fcaff", "palignr xmm1, xmm2, 0xff"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1090, "66450f3a0fcc00", "palignr xmm9, xmm12, 0"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x10a0, "66450f3a0fcc04", "palignr xmm9, xmm12, 4"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x10b0, "66450f3a0fcc08", "palignr xmm9, xmm12, 8"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x10c0, "66450f3a0fcc0f", "palignr xmm9, xmm12, 0xf"),      # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x10d0, "66450f3a0fcc10", "palignr xmm9, xmm12, 0x10"),     # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x10e0, "66450f3a0fcc11", "palignr xmm9, xmm12, 0x11"),     # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x10f0, "66450f3a0fcc1f", "palignr xmm9, xmm12, 0x1f"),     # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1100, "66450f3a0fcc20", "palignr xmm9, xmm12, 0x20"),     # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1110, "66450f3a0fccff", "palignr xmm9, xmm12, 0xff"),     # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1120, "660f3a0fdb00", "palignr xmm3, xmm3, 0"),           # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1130, "660f3a0fdb04", "palignr xmm3, xmm3, 4"),           # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1140, "660f3a0fdb08", "palignr xmm3, xmm3, 8"),           # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1150, "660f3a0fdb0f", "palignr xmm3, xmm3, 0xf"),         # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1160, "660f3a0fdb10", "palignr xmm3, xmm3, 0x10"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1170, "660f3a0fdb11", "palignr xmm3, xmm3, 0x11"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1180, "660f3a0fdb1f", "palignr xmm3, xmm3, 0x1f"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1190, "660f3a0fdb20", "palignr xmm3, xmm3, 0x20"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x11a0, "660f3a0fdbff", "palignr xmm3, xmm3, 0xff"),        # palignr | x,x,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x11b0, "660f3a0f0b00", "palignr xmm1, xmmword ptr [rbx], 0"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x11c0, "660f3a0f0b04", "palignr xmm1, xmmword ptr [rbx], 4"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x11d0, "660f3a0f0b08", "palignr xmm1, xmmword ptr [rbx], 8"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x11e0, "660f3a0f0b0f", "palignr xmm1, xmmword ptr [rbx], 0xf"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x11f0, "660f3a0f0b10", "palignr xmm1, xmmword ptr [rbx], 0x10"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1200, "660f3a0f0b11", "palignr xmm1, xmmword ptr [rbx], 0x11"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1210, "660f3a0f0b1f", "palignr xmm1, xmmword ptr [rbx], 0x1f"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1220, "660f3a0f0b20", "palignr xmm1, xmmword ptr [rbx], 0x20"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1230, "660f3a0f0bff", "palignr xmm1, xmmword ptr [rbx], 0xff"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1240, "660f3a0f0df66d000000", "palignr xmm1, xmmword ptr [rip + 0x6df6], 0"), # palignr | x,m128rip,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1250, "660f3a0f0de66d000004", "palignr xmm1, xmmword ptr [rip + 0x6de6], 4"), # palignr | x,m128rip,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1260, "660f3a0f0dd66d000008", "palignr xmm1, xmmword ptr [rip + 0x6dd6], 8"), # palignr | x,m128rip,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1270, "660f3a0f0dc66d00000f", "palignr xmm1, xmmword ptr [rip + 0x6dc6], 0xf"), # palignr | x,m128rip,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1280, "660f3a0f0db66d000010", "palignr xmm1, xmmword ptr [rip + 0x6db6], 0x10"), # palignr | x,m128rip,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1290, "660f3a0f0da66d000011", "palignr xmm1, xmmword ptr [rip + 0x6da6], 0x11"), # palignr | x,m128rip,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x12a0, "660f3a0f0d966d00001f", "palignr xmm1, xmmword ptr [rip + 0x6d96], 0x1f"), # palignr | x,m128rip,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x12b0, "660f3a0f0d866d000020", "palignr xmm1, xmmword ptr [rip + 0x6d86], 0x20"), # palignr | x,m128rip,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x12c0, "660f3a0f0d766d0000ff", "palignr xmm1, xmmword ptr [rip + 0x6d76], 0xff"), # palignr | x,m128rip,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x12d0, "66440f3a0f531000", "palignr xmm10, xmmword ptr [rbx + 0x10], 0"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x12e0, "66440f3a0f531004", "palignr xmm10, xmmword ptr [rbx + 0x10], 4"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x12f0, "66440f3a0f531008", "palignr xmm10, xmmword ptr [rbx + 0x10], 8"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1300, "66440f3a0f53100f", "palignr xmm10, xmmword ptr [rbx + 0x10], 0xf"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1310, "66440f3a0f531010", "palignr xmm10, xmmword ptr [rbx + 0x10], 0x10"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1320, "66440f3a0f531011", "palignr xmm10, xmmword ptr [rbx + 0x10], 0x11"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1330, "66440f3a0f53101f", "palignr xmm10, xmmword ptr [rbx + 0x10], 0x1f"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1340, "66440f3a0f531020", "palignr xmm10, xmmword ptr [rbx + 0x10], 0x20"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1350, "66440f3a0f5310ff", "palignr xmm10, xmmword ptr [rbx + 0x10], 0xff"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1360, "660f3a0f54882000", "palignr xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1370, "660f3a0f54882004", "palignr xmm2, xmmword ptr [rax + rcx*4 + 0x20], 4"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1380, "660f3a0f54882008", "palignr xmm2, xmmword ptr [rax + rcx*4 + 0x20], 8"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x1390, "660f3a0f5488200f", "palignr xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0xf"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x13a0, "660f3a0f54882010", "palignr xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0x10"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x13b0, "660f3a0f54882011", "palignr xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0x11"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x13c0, "660f3a0f5488201f", "palignr xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0x1f"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x13d0, "660f3a0f54882020", "palignr xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0x20"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
    (0x13e0, "660f3a0f548820ff", "palignr xmm2, xmmword ptr [rax + rcx*4 + 0x20], 0xff"), # palignr | x,m128,i | enc(0x0F3A0F, mand=0x66) | family sse_palignr
]
