"""Hand-written test vectors of the sse_minmax_s family: minss, maxss, minsd, maxsd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f30f5dca", "minss xmm1, xmm2"),
    (0x1010, "f3450f5dcc", "minss xmm9, xmm12"),
    (0x1020, "f30f5ddb", "minss xmm3, xmm3"),
    (0x1030, "f30f5d0b", "minss xmm1, dword ptr [rbx]"),
    (0x1040, "f3440f5d5310", "minss xmm10, dword ptr [rbx + 0x10]"),
    (0x1050, "f30f5d548820", "minss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f30f5d0dd86f0000", "minss xmm1, dword ptr [rip + 0x6fd8]"),
    (0x1070, "f30f5fca", "maxss xmm1, xmm2"),
    (0x1080, "f3450f5fcc", "maxss xmm9, xmm12"),
    (0x1090, "f30f5fdb", "maxss xmm3, xmm3"),
    (0x10a0, "f30f5f0b", "maxss xmm1, dword ptr [rbx]"),
    (0x10b0, "f3440f5f5310", "maxss xmm10, dword ptr [rbx + 0x10]"),
    (0x10c0, "f30f5f548820", "maxss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "f30f5f0d686f0000", "maxss xmm1, dword ptr [rip + 0x6f68]"),
    (0x10e0, "f20f5dca", "minsd xmm1, xmm2"),
    (0x10f0, "f2450f5dcc", "minsd xmm9, xmm12"),
    (0x1100, "f20f5ddb", "minsd xmm3, xmm3"),
    (0x1110, "f20f5d0b", "minsd xmm1, qword ptr [rbx]"),
    (0x1120, "f2440f5d5310", "minsd xmm10, qword ptr [rbx + 0x10]"),
    (0x1130, "f20f5d548820", "minsd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1140, "f20f5d0df86e0000", "minsd xmm1, qword ptr [rip + 0x6ef8]"),
    (0x1150, "f20f5fca", "maxsd xmm1, xmm2"),
    (0x1160, "f2450f5fcc", "maxsd xmm9, xmm12"),
    (0x1170, "f20f5fdb", "maxsd xmm3, xmm3"),
    (0x1180, "f20f5f0b", "maxsd xmm1, qword ptr [rbx]"),
    (0x1190, "f2440f5f5310", "maxsd xmm10, qword ptr [rbx + 0x10]"),
    (0x11a0, "f20f5f548820", "maxsd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x11b0, "f20f5f0d886e0000", "maxsd xmm1, qword ptr [rip + 0x6e88]"),
]
