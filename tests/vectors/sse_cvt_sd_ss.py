"""Hand-written test vectors of the sse_cvt_sd_ss family: cvtss2sd, cvtsd2ss.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f30f5aca", "cvtss2sd xmm1, xmm2"),
    (0x1010, "f3450f5acc", "cvtss2sd xmm9, xmm12"),
    (0x1020, "f30f5adb", "cvtss2sd xmm3, xmm3"),
    (0x1030, "f30f5a0b", "cvtss2sd xmm1, dword ptr [rbx]"),
    (0x1040, "f3440f5a5310", "cvtss2sd xmm10, dword ptr [rbx + 0x10]"),
    (0x1050, "f30f5a548820", "cvtss2sd xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f30f5a0dd86f0000", "cvtss2sd xmm1, dword ptr [rip + 0x6fd8]"),
    (0x1100, "f20f5aca", "cvtsd2ss xmm1, xmm2"),
    (0x1110, "f2450f5acc", "cvtsd2ss xmm9, xmm12"),
    (0x1120, "f20f5adb", "cvtsd2ss xmm3, xmm3"),
    (0x1130, "f20f5a0b", "cvtsd2ss xmm1, qword ptr [rbx]"),
    (0x1140, "f2440f5a5310", "cvtsd2ss xmm10, qword ptr [rbx + 0x10]"),
    (0x1150, "f20f5a548820", "cvtsd2ss xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "f20f5a0dd86e0000", "cvtsd2ss xmm1, qword ptr [rip + 0x6ed8]"),
]
