"""Hand-written test vectors of the sse_pminmax family: pmaxsw, pminsw, pmaxub and pminub, each with
the register forms (xmm, xmm; the same register twice; register 8 and above), the memory forms
(plain, displacement, SIB, RIP-relative at the aligned data 0x8040).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660feeca", "pmaxsw xmm1, xmm2"),                   # pmaxsw | x,x | enc(0x0FEE, mand=0x66) | family sse_pminmax
    (0x1010, "66450feecc", "pmaxsw xmm9, xmm12"),                # pmaxsw | x,x | enc(0x0FEE, mand=0x66) | family sse_pminmax
    (0x1020, "660feedb", "pmaxsw xmm3, xmm3"),                   # pmaxsw | x,x | enc(0x0FEE, mand=0x66) | family sse_pminmax
    (0x1030, "660fee0b", "pmaxsw xmm1, xmmword ptr [rbx]"),      # pmaxsw | x,m128 | enc(0x0FEE, mand=0x66) | family sse_pminmax
    (0x1040, "66440fee5310", "pmaxsw xmm10, xmmword ptr [rbx + 0x10]"), # pmaxsw | x,m128 | enc(0x0FEE, mand=0x66) | family sse_pminmax
    (0x1050, "660fee548820", "pmaxsw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # pmaxsw | x,m128 | enc(0x0FEE, mand=0x66) | family sse_pminmax
    (0x1060, "660fee0dd86f0000", "pmaxsw xmm1, xmmword ptr [rip + 0x6fd8]"), # pmaxsw | x,m128rip | enc(0x0FEE, mand=0x66) | family sse_pminmax
    (0x1100, "660feaca", "pminsw xmm1, xmm2"),                   # pminsw | x,x | enc(0x0FEA, mand=0x66) | family sse_pminmax
    (0x1110, "66450feacc", "pminsw xmm9, xmm12"),                # pminsw | x,x | enc(0x0FEA, mand=0x66) | family sse_pminmax
    (0x1120, "660feadb", "pminsw xmm3, xmm3"),                   # pminsw | x,x | enc(0x0FEA, mand=0x66) | family sse_pminmax
    (0x1130, "660fea0b", "pminsw xmm1, xmmword ptr [rbx]"),      # pminsw | x,m128 | enc(0x0FEA, mand=0x66) | family sse_pminmax
    (0x1140, "66440fea5310", "pminsw xmm10, xmmword ptr [rbx + 0x10]"), # pminsw | x,m128 | enc(0x0FEA, mand=0x66) | family sse_pminmax
    (0x1150, "660fea548820", "pminsw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # pminsw | x,m128 | enc(0x0FEA, mand=0x66) | family sse_pminmax
    (0x1160, "660fea0dd86e0000", "pminsw xmm1, xmmword ptr [rip + 0x6ed8]"), # pminsw | x,m128rip | enc(0x0FEA, mand=0x66) | family sse_pminmax
    (0x1200, "660fdeca", "pmaxub xmm1, xmm2"),                   # pmaxub | x,x | enc(0x0FDE, mand=0x66) | family sse_pminmax
    (0x1210, "66450fdecc", "pmaxub xmm9, xmm12"),                # pmaxub | x,x | enc(0x0FDE, mand=0x66) | family sse_pminmax
    (0x1220, "660fdedb", "pmaxub xmm3, xmm3"),                   # pmaxub | x,x | enc(0x0FDE, mand=0x66) | family sse_pminmax
    (0x1230, "660fde0b", "pmaxub xmm1, xmmword ptr [rbx]"),      # pmaxub | x,m128 | enc(0x0FDE, mand=0x66) | family sse_pminmax
    (0x1240, "66440fde5310", "pmaxub xmm10, xmmword ptr [rbx + 0x10]"), # pmaxub | x,m128 | enc(0x0FDE, mand=0x66) | family sse_pminmax
    (0x1250, "660fde548820", "pmaxub xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # pmaxub | x,m128 | enc(0x0FDE, mand=0x66) | family sse_pminmax
    (0x1260, "660fde0dd86d0000", "pmaxub xmm1, xmmword ptr [rip + 0x6dd8]"), # pmaxub | x,m128rip | enc(0x0FDE, mand=0x66) | family sse_pminmax
    (0x1300, "660fdaca", "pminub xmm1, xmm2"),                   # pminub | x,x | enc(0x0FDA, mand=0x66) | family sse_pminmax
    (0x1310, "66450fdacc", "pminub xmm9, xmm12"),                # pminub | x,x | enc(0x0FDA, mand=0x66) | family sse_pminmax
    (0x1320, "660fdadb", "pminub xmm3, xmm3"),                   # pminub | x,x | enc(0x0FDA, mand=0x66) | family sse_pminmax
    (0x1330, "660fda0b", "pminub xmm1, xmmword ptr [rbx]"),      # pminub | x,m128 | enc(0x0FDA, mand=0x66) | family sse_pminmax
    (0x1340, "66440fda5310", "pminub xmm10, xmmword ptr [rbx + 0x10]"), # pminub | x,m128 | enc(0x0FDA, mand=0x66) | family sse_pminmax
    (0x1350, "660fda548820", "pminub xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # pminub | x,m128 | enc(0x0FDA, mand=0x66) | family sse_pminmax
    (0x1360, "660fda0dd86c0000", "pminub xmm1, xmmword ptr [rip + 0x6cd8]"), # pminub | x,m128rip | enc(0x0FDA, mand=0x66) | family sse_pminmax
]
