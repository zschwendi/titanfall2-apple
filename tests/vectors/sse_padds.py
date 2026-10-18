"""Hand-written test vectors of the sse_padds family: paddsb, paddsw, paddusb and paddusw, the
saturating packed adds (66 0F EC, ED, DC and DD /r), each with the forms xmm,xmm (including the
same register twice and a register numbered 8 or above), xmm,m128 and xmm,m128 RIP-relative (the
data at 0x8040, aligned). paddsw's MMX form (no 66 prefix, mm,m64 RIP-relative) is refused: it is
not this family's encoding.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660fecca", "paddsb xmm1, xmm2"),
    (0x1010, "66450feccc", "paddsb xmm9, xmm12"),
    (0x1020, "660fecdb", "paddsb xmm3, xmm3"),
    (0x1030, "660fec0b", "paddsb xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440fec5310", "paddsb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660fec548820", "paddsb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660fec0dd86f0000", "paddsb xmm1, xmmword ptr [rip + 0x6fd8]"),
    (0x1100, "660fedca", "paddsw xmm1, xmm2"),
    (0x1110, "66450fedcc", "paddsw xmm9, xmm12"),
    (0x1120, "660feddb", "paddsw xmm3, xmm3"),
    (0x1130, "660fed0b", "paddsw xmm1, xmmword ptr [rbx]"),
    (0x1140, "66440fed5310", "paddsw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "660fed548820", "paddsw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "660fed0dd86e0000", "paddsw xmm1, xmmword ptr [rip + 0x6ed8]"),
    (0x1200, "660fdcca", "paddusb xmm1, xmm2"),
    (0x1210, "66450fdccc", "paddusb xmm9, xmm12"),
    (0x1220, "660fdcdb", "paddusb xmm3, xmm3"),
    (0x1230, "660fdc0b", "paddusb xmm1, xmmword ptr [rbx]"),
    (0x1240, "66440fdc5310", "paddusb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "660fdc548820", "paddusb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "660fdc0dd86d0000", "paddusb xmm1, xmmword ptr [rip + 0x6dd8]"),
    (0x1300, "660fddca", "paddusw xmm1, xmm2"),
    (0x1310, "66450fddcc", "paddusw xmm9, xmm12"),
    (0x1320, "660fdddb", "paddusw xmm3, xmm3"),
    (0x1330, "660fdd0b", "paddusw xmm1, xmmword ptr [rbx]"),
    (0x1340, "66440fdd5310", "paddusw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1350, "660fdd548820", "paddusw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "660fdd0dd86c0000", "paddusw xmm1, xmmword ptr [rip + 0x6cd8]"),
]

REFUSED = [
    (0x1370, "0fed0dc96c0000", "paddsw mm1, qword ptr [rip + 0x6cc9]", "paddsw"),
]
