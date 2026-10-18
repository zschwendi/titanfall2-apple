"""Hand-written test vectors of the sse_punpckh family: punpckhbw, punpckhwd, punpckhdq and
punpckhqdq, the xmm forms (66 0F 68, 69, 6A, 6D /r) with register-register, register-memory,
RIP-relative and register 8 and above (xmm9, xmm12, xmm15 among them), and a register with itself.
The RIP-relative operands point at the data at 0x8040, which is 16-byte aligned. The MMX forms
(no 66 prefix) are refused.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660f68ca", "punpckhbw xmm1, xmm2"),
    (0x1010, "66450f68cc", "punpckhbw xmm9, xmm12"),
    (0x1020, "660f68db", "punpckhbw xmm3, xmm3"),
    (0x1030, "660f680b", "punpckhbw xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440f685310", "punpckhbw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660f68548820", "punpckhbw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660f680dd86f0000", "punpckhbw xmm1, xmmword ptr [rip + 0x6fd8]"),
    (0x1100, "660f69ca", "punpckhwd xmm1, xmm2"),
    (0x1110, "66450f69cc", "punpckhwd xmm9, xmm12"),
    (0x1120, "660f69db", "punpckhwd xmm3, xmm3"),
    (0x1130, "660f690b", "punpckhwd xmm1, xmmword ptr [rbx]"),
    (0x1140, "66440f695310", "punpckhwd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "660f69548820", "punpckhwd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "660f690dd86e0000", "punpckhwd xmm1, xmmword ptr [rip + 0x6ed8]"),
    (0x1200, "660f6aca", "punpckhdq xmm1, xmm2"),
    (0x1210, "66450f6acc", "punpckhdq xmm9, xmm12"),
    (0x1220, "660f6adb", "punpckhdq xmm3, xmm3"),
    (0x1230, "660f6a0b", "punpckhdq xmm1, xmmword ptr [rbx]"),
    (0x1240, "66440f6a5310", "punpckhdq xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "660f6a548820", "punpckhdq xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "660f6a0dd86d0000", "punpckhdq xmm1, xmmword ptr [rip + 0x6dd8]"),
    (0x1300, "660f6dca", "punpckhqdq xmm1, xmm2"),
    (0x1310, "66450f6dcc", "punpckhqdq xmm9, xmm12"),
    (0x1320, "660f6ddb", "punpckhqdq xmm3, xmm3"),
    (0x1330, "660f6d0b", "punpckhqdq xmm1, xmmword ptr [rbx]"),
    (0x1340, "66440f6d5310", "punpckhqdq xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1350, "660f6d548820", "punpckhqdq xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "660f6d0dd86c0000", "punpckhqdq xmm1, xmmword ptr [rip + 0x6cd8]"),
]

REFUSED = [
    (0x1400, "0f68ca", "punpckhbw mm1, mm2", "punpckhbw"),
    (0x1420, "0f68db", "punpckhbw mm3, mm3", "punpckhbw"),
    (0x1430, "0f680b", "punpckhbw mm1, qword ptr [rbx]", "punpckhbw"),
    (0x1450, "0f68548820", "punpckhbw mm2, qword ptr [rax + rcx*4 + 0x20]", "punpckhbw"),
    (0x1460, "0f680dd96b0000", "punpckhbw mm1, qword ptr [rip + 0x6bd9]", "punpckhbw"),
    (0x1500, "0f69ca", "punpckhwd mm1, mm2", "punpckhwd"),
    (0x1520, "0f69db", "punpckhwd mm3, mm3", "punpckhwd"),
    (0x1530, "0f690b", "punpckhwd mm1, qword ptr [rbx]", "punpckhwd"),
    (0x1550, "0f69548820", "punpckhwd mm2, qword ptr [rax + rcx*4 + 0x20]", "punpckhwd"),
    (0x1560, "0f690dd96a0000", "punpckhwd mm1, qword ptr [rip + 0x6ad9]", "punpckhwd"),
    (0x1600, "0f6aca", "punpckhdq mm1, mm2", "punpckhdq"),
    (0x1620, "0f6adb", "punpckhdq mm3, mm3", "punpckhdq"),
    (0x1630, "0f6a0b", "punpckhdq mm1, qword ptr [rbx]", "punpckhdq"),
    (0x1650, "0f6a548820", "punpckhdq mm2, qword ptr [rax + rcx*4 + 0x20]", "punpckhdq"),
    (0x1660, "0f6a0dd9690000", "punpckhdq mm1, qword ptr [rip + 0x69d9]", "punpckhdq"),
]
