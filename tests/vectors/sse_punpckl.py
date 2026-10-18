"""Hand-written test vectors of the sse_punpckl family: punpcklbw, punpcklwd, punpckldq and
punpcklqdq, the interleaves of the low halves, each with an xmm register and a register numbered
8 or above, a register with itself, and 16 bytes of memory at a base, an index and rip.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660f60ca", "punpcklbw xmm1, xmm2"),                # punpcklbw | x,x | enc(0x0F60, mand=0x66)
    (0x1010, "66450f60cc", "punpcklbw xmm9, xmm12"),             # punpcklbw | x,x | enc(0x0F60, mand=0x66)
    (0x1020, "660f60db", "punpcklbw xmm3, xmm3"),                # punpcklbw | x,x | enc(0x0F60, mand=0x66)
    (0x1030, "660f600b", "punpcklbw xmm1, xmmword ptr [rbx]"),   # punpcklbw | x,m128 | enc(0x0F60, mand=0x66)
    (0x1040, "66440f605310", "punpcklbw xmm10, xmmword ptr [rbx + 0x10]"), # punpcklbw | x,m128 | enc(0x0F60, mand=0x66)
    (0x1050, "660f60548820", "punpcklbw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # punpcklbw | x,m128 | enc(0x0F60, mand=0x66)
    (0x1060, "660f600dd86f0000", "punpcklbw xmm1, xmmword ptr [rip + 0x6fd8]"), # punpcklbw | x,m128rip | enc(0x0F60, mand=0x66)
    (0x1100, "660f61ca", "punpcklwd xmm1, xmm2"),                # punpcklwd | x,x | enc(0x0F61, mand=0x66)
    (0x1110, "66450f61cc", "punpcklwd xmm9, xmm12"),             # punpcklwd | x,x | enc(0x0F61, mand=0x66)
    (0x1120, "660f61db", "punpcklwd xmm3, xmm3"),                # punpcklwd | x,x | enc(0x0F61, mand=0x66)
    (0x1130, "660f610b", "punpcklwd xmm1, xmmword ptr [rbx]"),   # punpcklwd | x,m128 | enc(0x0F61, mand=0x66)
    (0x1140, "66440f615310", "punpcklwd xmm10, xmmword ptr [rbx + 0x10]"), # punpcklwd | x,m128 | enc(0x0F61, mand=0x66)
    (0x1150, "660f61548820", "punpcklwd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # punpcklwd | x,m128 | enc(0x0F61, mand=0x66)
    (0x1160, "660f610dd86e0000", "punpcklwd xmm1, xmmword ptr [rip + 0x6ed8]"), # punpcklwd | x,m128rip | enc(0x0F61, mand=0x66)
    (0x1200, "660f62ca", "punpckldq xmm1, xmm2"),                # punpckldq | x,x | enc(0x0F62, mand=0x66)
    (0x1210, "66450f62cc", "punpckldq xmm9, xmm12"),             # punpckldq | x,x | enc(0x0F62, mand=0x66)
    (0x1220, "660f62db", "punpckldq xmm3, xmm3"),                # punpckldq | x,x | enc(0x0F62, mand=0x66)
    (0x1230, "660f620b", "punpckldq xmm1, xmmword ptr [rbx]"),   # punpckldq | x,m128 | enc(0x0F62, mand=0x66)
    (0x1240, "66440f625310", "punpckldq xmm10, xmmword ptr [rbx + 0x10]"), # punpckldq | x,m128 | enc(0x0F62, mand=0x66)
    (0x1250, "660f62548820", "punpckldq xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # punpckldq | x,m128 | enc(0x0F62, mand=0x66)
    (0x1260, "660f620dd86d0000", "punpckldq xmm1, xmmword ptr [rip + 0x6dd8]"), # punpckldq | x,m128rip | enc(0x0F62, mand=0x66)
    (0x1300, "660f6cca", "punpcklqdq xmm1, xmm2"),               # punpcklqdq | x,x | enc(0x0F6C, mand=0x66)
    (0x1310, "66450f6ccc", "punpcklqdq xmm9, xmm12"),            # punpcklqdq | x,x | enc(0x0F6C, mand=0x66)
    (0x1320, "660f6cdb", "punpcklqdq xmm3, xmm3"),               # punpcklqdq | x,x | enc(0x0F6C, mand=0x66)
    (0x1330, "660f6c0b", "punpcklqdq xmm1, xmmword ptr [rbx]"),  # punpcklqdq | x,m128 | enc(0x0F6C, mand=0x66)
    (0x1340, "66440f6c5310", "punpcklqdq xmm10, xmmword ptr [rbx + 0x10]"), # punpcklqdq | x,m128 | enc(0x0F6C, mand=0x66)
    (0x1350, "660f6c548820", "punpcklqdq xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # punpcklqdq | x,m128 | enc(0x0F6C, mand=0x66)
    (0x1360, "660f6c0dd86c0000", "punpcklqdq xmm1, xmmword ptr [rip + 0x6cd8]"), # punpcklqdq | x,m128rip | enc(0x0F6C, mand=0x66)
]

# The MMX forms (no 66 prefix), which the game has and the lifter leaves unlifted and reported.
REFUSED = [
    (0x1400, "0f600b", "punpcklbw mm1, dword ptr [rbx]", "opcode f60 /1 is not a form the lifter translates"),
    (0x1410, "0f610b", "punpcklwd mm1, dword ptr [rbx]", "opcode f61 /1 is not a form the lifter translates"),
    (0x1420, "0f620b", "punpckldq mm1, dword ptr [rbx]", "opcode f62 /1 is not a form the lifter translates"),
]
