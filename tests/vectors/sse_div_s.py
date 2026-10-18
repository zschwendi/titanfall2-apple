"""Hand-written test vectors of the sse_div_s family: divss, divsd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1100, "f30f5eca", "divss xmm1, xmm2"),
    (0x1110, "f3450f5ecc", "divss xmm9, xmm12"),
    (0x1120, "f30f5edb", "divss xmm3, xmm3"),
    (0x1130, "f30f5e0b", "divss xmm1, dword ptr [rbx]"),
    (0x1140, "f3440f5e5310", "divss xmm10, dword ptr [rbx + 0x10]"),
    (0x1150, "f30f5e548820", "divss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "f30f5e0dd86e0000", "divss xmm1, dword ptr [rip + 0x6ed8]"),
    (0x1200, "f20f5eca", "divsd xmm1, xmm2"),
    (0x1210, "f2450f5ecc", "divsd xmm9, xmm12"),
    (0x1220, "f20f5edb", "divsd xmm3, xmm3"),
    (0x1230, "f20f5e0b", "divsd xmm1, qword ptr [rbx]"),
    (0x1240, "f2440f5e5310", "divsd xmm10, qword ptr [rbx + 0x10]"),
    (0x1250, "f20f5e548820", "divsd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "f20f5e0dd86d0000", "divsd xmm1, qword ptr [rip + 0x6dd8]"),
]
