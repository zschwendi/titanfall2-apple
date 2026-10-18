"""Hand-written test vectors of the sse_sqrt_s family: sqrtss, sqrtsd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f30f51ca", "sqrtss xmm1, xmm2"),
    (0x1010, "f3450f51cc", "sqrtss xmm9, xmm12"),
    (0x1020, "f30f51db", "sqrtss xmm3, xmm3"),
    (0x1030, "f30f510b", "sqrtss xmm1, dword ptr [rbx]"),
    (0x1040, "f3440f515310", "sqrtss xmm10, dword ptr [rbx + 0x10]"),
    (0x1050, "f30f51548820", "sqrtss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f30f510dd86f0000", "sqrtss xmm1, dword ptr [rip + 0x6fd8]"),
    (0x1100, "f20f51ca", "sqrtsd xmm1, xmm2"),
    (0x1110, "f2450f51cc", "sqrtsd xmm9, xmm12"),
    (0x1120, "f20f51db", "sqrtsd xmm3, xmm3"),
    (0x1130, "f20f510b", "sqrtsd xmm1, qword ptr [rbx]"),
    (0x1140, "f2440f515310", "sqrtsd xmm10, qword ptr [rbx + 0x10]"),
    (0x1150, "f20f51548820", "sqrtsd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "f20f510dd86e0000", "sqrtsd xmm1, qword ptr [rip + 0x6ed8]"),
]
