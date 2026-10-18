"""Hand-written test vectors of the sse_scalar_arith family: addss, addsd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f30f58ca", "addss xmm1, xmm2"),
    (0x1010, "f30f580b", "addss xmm1, dword ptr [rbx]"),
    (0x1020, "f3450f58c7", "addss xmm8, xmm15"),
    (0x1030, "f30f58c0", "addss xmm0, xmm0"),
    (0x1040, "f30f580dc86f0000", "addss xmm1, dword ptr [rip + 0x6fc8]"),
    (0x1050, "f30f585c2420", "addss xmm3, dword ptr [rsp + 0x20]"),
    (0x1060, "f20f58ca", "addsd xmm1, xmm2"),
    (0x1070, "f20f580b", "addsd xmm1, qword ptr [rbx]"),
    (0x1080, "f2450f58ca", "addsd xmm9, xmm10"),
    (0x1090, "f20f58c0", "addsd xmm0, xmm0"),
    (0x10a0, "f20f5815806f0000", "addsd xmm2, qword ptr [rip + 0x6f80]"),
    (0x10b0, "f20f585c2420", "addsd xmm3, qword ptr [rsp + 0x20]"),
]
