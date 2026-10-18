"""Hand-written test vectors of the sse_comi family: comiss, ucomiss, comisd, ucomisd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "0f2fca", "comiss xmm1, xmm2"),
    (0x1010, "450f2fcc", "comiss xmm9, xmm12"),
    (0x1020, "0f2fdb", "comiss xmm3, xmm3"),
    (0x1030, "0f2f0b", "comiss xmm1, dword ptr [rbx]"),
    (0x1040, "440f2f5310", "comiss xmm10, dword ptr [rbx + 0x10]"),
    (0x1050, "0f2f548820", "comiss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "0f2f0dd96f0000", "comiss xmm1, dword ptr [rip + 0x6fd9]"),
    (0x1100, "0f2eca", "ucomiss xmm1, xmm2"),
    (0x1110, "450f2ecc", "ucomiss xmm9, xmm12"),
    (0x1120, "0f2edb", "ucomiss xmm3, xmm3"),
    (0x1130, "0f2e0b", "ucomiss xmm1, dword ptr [rbx]"),
    (0x1140, "440f2e5310", "ucomiss xmm10, dword ptr [rbx + 0x10]"),
    (0x1150, "0f2e548820", "ucomiss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "0f2e0dd96e0000", "ucomiss xmm1, dword ptr [rip + 0x6ed9]"),
    (0x1200, "660f2fca", "comisd xmm1, xmm2"),
    (0x1210, "66450f2fcc", "comisd xmm9, xmm12"),
    (0x1220, "660f2fdb", "comisd xmm3, xmm3"),
    (0x1230, "660f2f0b", "comisd xmm1, xmmword ptr [rbx]"),
    (0x1240, "66440f2f5310", "comisd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "660f2f548820", "comisd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "660f2f0dd86d0000", "comisd xmm1, xmmword ptr [rip + 0x6dd8]"),
    (0x1300, "660f2eca", "ucomisd xmm1, xmm2"),
    (0x1310, "66450f2ecc", "ucomisd xmm9, xmm12"),
    (0x1320, "660f2edb", "ucomisd xmm3, xmm3"),
    (0x1330, "660f2e0b", "ucomisd xmm1, qword ptr [rbx]"),
    (0x1340, "66440f2e5310", "ucomisd xmm10, qword ptr [rbx + 0x10]"),
    (0x1350, "660f2e548820", "ucomisd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "660f2e0dd86c0000", "ucomisd xmm1, qword ptr [rip + 0x6cd8]"),
]
