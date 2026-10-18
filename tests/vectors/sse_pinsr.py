"""Hand-written test vectors of the sse_pinsr family: pinsrw, pinsrb and pinsrd, each with the
register (r32), memory and RIP-relative source forms of the game, with immediates 0, 8, 0x1b and
0xff (lane wrap-around, the top lane) and with a high register (xmm9, xmm10, r12d) in some lines.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660fc4ca00", "pinsrw xmm1, edx, 0"),               # pinsrw | x,r32,i | enc(0x0FC4, mand=0x66) | family sse_pinsr
    (0x1010, "66450fc4cc08", "pinsrw xmm9, r12d, 8"),            # pinsrw | x,r32,i | enc(0x0FC4, mand=0x66) | family sse_pinsr
    (0x1020, "660fc4db1b", "pinsrw xmm3, ebx, 0x1b"),            # pinsrw | x,r32,i | enc(0x0FC4, mand=0x66) | family sse_pinsr
    (0x1030, "660fc40bff", "pinsrw xmm1, word ptr [rbx], 0xff"), # pinsrw | x,m16,i | enc(0x0FC4, mand=0x66) | family sse_pinsr
    (0x1040, "66440fc4531000", "pinsrw xmm10, word ptr [rbx + 0x10], 0"), # pinsrw | x,m16,i | enc(0x0FC4, mand=0x66) | family sse_pinsr
    (0x1050, "660fc454882008", "pinsrw xmm2, word ptr [rax + rcx*4 + 0x20], 8"), # pinsrw | x,m16,i | enc(0x0FC4, mand=0x66) | family sse_pinsr
    (0x1060, "660fc40dd76f00001b", "pinsrw xmm1, word ptr [rip + 0x6fd7], 0x1b"), # pinsrw | x,m16rip,i | enc(0x0FC4, mand=0x66) | family sse_pinsr
    (0x1070, "660fc4ca08", "pinsrw xmm1, edx, 8"),               # pinsrw | x,r32,i | enc(0x0FC4, mand=0x66) | family sse_pinsr
    (0x1080, "660fc4ca1b", "pinsrw xmm1, edx, 0x1b"),            # pinsrw | x,r32,i | enc(0x0FC4, mand=0x66) | family sse_pinsr
    (0x1090, "660fc4caff", "pinsrw xmm1, edx, 0xff"),            # pinsrw | x,r32,i | enc(0x0FC4, mand=0x66) | family sse_pinsr
    (0x1100, "660f3a20ca00", "pinsrb xmm1, edx, 0"),             # pinsrb | x,r32,i | enc(0x0F3A20, mand=0x66) | family sse_pinsr
    (0x1110, "66450f3a20cc08", "pinsrb xmm9, r12d, 8"),          # pinsrb | x,r32,i | enc(0x0F3A20, mand=0x66) | family sse_pinsr
    (0x1120, "660f3a20db1b", "pinsrb xmm3, ebx, 0x1b"),          # pinsrb | x,r32,i | enc(0x0F3A20, mand=0x66) | family sse_pinsr
    (0x1130, "660f3a200bff", "pinsrb xmm1, byte ptr [rbx], 0xff"), # pinsrb | x,m8,i | enc(0x0F3A20, mand=0x66) | family sse_pinsr
    (0x1140, "66440f3a20531000", "pinsrb xmm10, byte ptr [rbx + 0x10], 0"), # pinsrb | x,m8,i | enc(0x0F3A20, mand=0x66) | family sse_pinsr
    (0x1150, "660f3a2054882008", "pinsrb xmm2, byte ptr [rax + rcx*4 + 0x20], 8"), # pinsrb | x,m8,i | enc(0x0F3A20, mand=0x66) | family sse_pinsr
    (0x1160, "660f3a200dd66e00001b", "pinsrb xmm1, byte ptr [rip + 0x6ed6], 0x1b"), # pinsrb | x,m8rip,i | enc(0x0F3A20, mand=0x66) | family sse_pinsr
    (0x1170, "660f3a20ca08", "pinsrb xmm1, edx, 8"),             # pinsrb | x,r32,i | enc(0x0F3A20, mand=0x66) | family sse_pinsr
    (0x1180, "660f3a20ca1b", "pinsrb xmm1, edx, 0x1b"),          # pinsrb | x,r32,i | enc(0x0F3A20, mand=0x66) | family sse_pinsr
    (0x1190, "660f3a20caff", "pinsrb xmm1, edx, 0xff"),          # pinsrb | x,r32,i | enc(0x0F3A20, mand=0x66) | family sse_pinsr
    (0x1200, "660f3a22ca00", "pinsrd xmm1, edx, 0"),             # pinsrd | x,r32,i | enc(0x0F3A22, mand=0x66) | family sse_pinsr
    (0x1210, "66450f3a22cc08", "pinsrd xmm9, r12d, 8"),          # pinsrd | x,r32,i | enc(0x0F3A22, mand=0x66) | family sse_pinsr
    (0x1220, "660f3a22db1b", "pinsrd xmm3, ebx, 0x1b"),          # pinsrd | x,r32,i | enc(0x0F3A22, mand=0x66) | family sse_pinsr
    (0x1230, "660f3a220bff", "pinsrd xmm1, dword ptr [rbx], 0xff"), # pinsrd | x,m32,i | enc(0x0F3A22, mand=0x66) | family sse_pinsr
    (0x1240, "66440f3a22531000", "pinsrd xmm10, dword ptr [rbx + 0x10], 0"), # pinsrd | x,m32,i | enc(0x0F3A22, mand=0x66) | family sse_pinsr
    (0x1250, "660f3a2254882008", "pinsrd xmm2, dword ptr [rax + rcx*4 + 0x20], 8"), # pinsrd | x,m32,i | enc(0x0F3A22, mand=0x66) | family sse_pinsr
    (0x1260, "660f3a220dd66d00001b", "pinsrd xmm1, dword ptr [rip + 0x6dd6], 0x1b"), # pinsrd | x,m32rip,i | enc(0x0F3A22, mand=0x66) | family sse_pinsr
    (0x1270, "660f3a22ca08", "pinsrd xmm1, edx, 8"),             # pinsrd | x,r32,i | enc(0x0F3A22, mand=0x66) | family sse_pinsr
    (0x1280, "660f3a22ca1b", "pinsrd xmm1, edx, 0x1b"),          # pinsrd | x,r32,i | enc(0x0F3A22, mand=0x66) | family sse_pinsr
    (0x1290, "660f3a22caff", "pinsrd xmm1, edx, 0xff"),          # pinsrd | x,r32,i | enc(0x0F3A22, mand=0x66) | family sse_pinsr
]
