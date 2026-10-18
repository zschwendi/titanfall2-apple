"""Hand-written test vectors of the sse_mul_s family: mulss, mulsd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f30f59ca", "mulss xmm1, xmm2"),
    (0x1010, "f3450f59cc", "mulss xmm9, xmm12"),
    (0x1020, "f30f59db", "mulss xmm3, xmm3"),
    (0x1030, "f30f590b", "mulss xmm1, dword ptr [rbx]"),
    (0x1040, "f3440f595310", "mulss xmm10, dword ptr [rbx + 0x10]"),
    (0x1050, "f30f59548820", "mulss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f30f590dd86f0000", "mulss xmm1, dword ptr [rip + 0x6fd8]"),
    (0x1070, "f20f59ca", "mulsd xmm1, xmm2"),
    (0x1080, "f2450f59cc", "mulsd xmm9, xmm12"),
    (0x1090, "f20f59db", "mulsd xmm3, xmm3"),
    (0x10a0, "f20f590b", "mulsd xmm1, qword ptr [rbx]"),
    (0x10b0, "f2440f595310", "mulsd xmm10, qword ptr [rbx + 0x10]"),
    (0x10c0, "f20f59548820", "mulsd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "f20f590dd86f0000", "mulsd xmm1, qword ptr [rip + 0x6fd8]"),
]
