"""Hand-written test vectors of the sse_sub_s family: subss, subsd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f30f5cca", "subss xmm1, xmm2"),
    (0x1010, "f3450f5ccc", "subss xmm9, xmm12"),
    (0x1020, "f30f5cdb", "subss xmm3, xmm3"),
    (0x1030, "f30f5c0b", "subss xmm1, dword ptr [rbx]"),
    (0x1040, "f3440f5c5310", "subss xmm10, dword ptr [rbx + 0x10]"),
    (0x1050, "f30f5c548820", "subss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f30f5c0dd86f0000", "subss xmm1, dword ptr [rip + 0x6fd8]"),
    (0x1100, "f20f5cca", "subsd xmm1, xmm2"),
    (0x1110, "f2450f5ccc", "subsd xmm9, xmm12"),
    (0x1120, "f20f5cdb", "subsd xmm3, xmm3"),
    (0x1130, "f20f5c0b", "subsd xmm1, qword ptr [rbx]"),
    (0x1140, "f2440f5c5310", "subsd xmm10, qword ptr [rbx + 0x10]"),
    (0x1150, "f20f5c548820", "subsd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "f20f5c0dd86e0000", "subsd xmm1, qword ptr [rip + 0x6ed8]"),
]
