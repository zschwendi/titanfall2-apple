"""Hand-written test vectors of the sse_cvt_s2si family: cvtss2si, cvtsd2si.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f30f2dca", "cvtss2si ecx, xmm2"),
    (0x1010, "f3450f2dcc", "cvtss2si r9d, xmm12"),
    (0x1020, "f30f2ddb", "cvtss2si ebx, xmm3"),
    (0x1030, "f30f2d0b", "cvtss2si ecx, dword ptr [rbx]"),
    (0x1040, "f3440f2d5310", "cvtss2si r10d, dword ptr [rbx + 0x10]"),
    (0x1050, "f30f2d548820", "cvtss2si edx, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f30f2d0dd86f0000", "cvtss2si ecx, dword ptr [rip + 0x6fd8]"),
    (0x1100, "f3480f2dca", "cvtss2si rcx, xmm2"),
    (0x1110, "f34d0f2dcc", "cvtss2si r9, xmm12"),
    (0x1120, "f3480f2ddb", "cvtss2si rbx, xmm3"),
    (0x1130, "f3480f2d0b", "cvtss2si rcx, dword ptr [rbx]"),
    (0x1140, "f34c0f2d5310", "cvtss2si r10, dword ptr [rbx + 0x10]"),
    (0x1150, "f3480f2d548820", "cvtss2si rdx, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "f3480f2d0dd76e0000", "cvtss2si rcx, dword ptr [rip + 0x6ed7]"),
    (0x1200, "f20f2dca", "cvtsd2si ecx, xmm2"),
    (0x1210, "f2450f2dcc", "cvtsd2si r9d, xmm12"),
    (0x1220, "f20f2ddb", "cvtsd2si ebx, xmm3"),
    (0x1230, "f20f2d0b", "cvtsd2si ecx, qword ptr [rbx]"),
    (0x1240, "f2440f2d5310", "cvtsd2si r10d, qword ptr [rbx + 0x10]"),
    (0x1250, "f20f2d548820", "cvtsd2si edx, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "f20f2d0dd86d0000", "cvtsd2si ecx, qword ptr [rip + 0x6dd8]"),
    (0x1300, "f2480f2dca", "cvtsd2si rcx, xmm2"),
    (0x1310, "f24d0f2dcc", "cvtsd2si r9, xmm12"),
    (0x1320, "f2480f2ddb", "cvtsd2si rbx, xmm3"),
    (0x1330, "f2480f2d0b", "cvtsd2si rcx, qword ptr [rbx]"),
    (0x1340, "f24c0f2d5310", "cvtsd2si r10, qword ptr [rbx + 0x10]"),
    (0x1350, "f2480f2d548820", "cvtsd2si rdx, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "f2480f2d0dd76c0000", "cvtsd2si rcx, qword ptr [rip + 0x6cd7]"),
    (0x1400, "f30f2dc0", "cvtss2si eax, xmm0"),
    (0x1410, "f3480f2dc0", "cvtss2si rax, xmm0"),
    (0x1420, "f20f2dc0", "cvtsd2si eax, xmm0"),
    (0x1430, "f2480f2dc0", "cvtsd2si rax, xmm0"),
]
