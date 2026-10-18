"""Hand-written test vectors of the sse_cvtsi2s family: cvtsi2ss, cvtsi2sd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f30f2aca", "cvtsi2ss xmm1, edx"),
    (0x1010, "f3450f2acc", "cvtsi2ss xmm9, r12d"),
    (0x1020, "f30f2adb", "cvtsi2ss xmm3, ebx"),
    (0x1030, "f30f2a0b", "cvtsi2ss xmm1, dword ptr [rbx]"),
    (0x1040, "f3440f2a5310", "cvtsi2ss xmm10, dword ptr [rbx + 0x10]"),
    (0x1050, "f30f2a548820", "cvtsi2ss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f30f2a0dd86f0000", "cvtsi2ss xmm1, dword ptr [rip + 0x6fd8]"),
    (0x1070, "f3480f2aca", "cvtsi2ss xmm1, rdx"),
    (0x1080, "f34d0f2acc", "cvtsi2ss xmm9, r12"),
    (0x1090, "f3480f2adb", "cvtsi2ss xmm3, rbx"),
    (0x10a0, "f3480f2a0b", "cvtsi2ss xmm1, qword ptr [rbx]"),
    (0x10b0, "f34c0f2a5310", "cvtsi2ss xmm10, qword ptr [rbx + 0x10]"),
    (0x10c0, "f3480f2a548820", "cvtsi2ss xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "f3480f2a0d676f0000", "cvtsi2ss xmm1, qword ptr [rip + 0x6f67]"),
    (0x10e0, "f20f2aca", "cvtsi2sd xmm1, edx"),
    (0x10f0, "f2450f2acc", "cvtsi2sd xmm9, r12d"),
    (0x1100, "f20f2adb", "cvtsi2sd xmm3, ebx"),
    (0x1110, "f20f2a0b", "cvtsi2sd xmm1, dword ptr [rbx]"),
    (0x1120, "f2440f2a5310", "cvtsi2sd xmm10, dword ptr [rbx + 0x10]"),
    (0x1130, "f20f2a548820", "cvtsi2sd xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1140, "f20f2a0df86e0000", "cvtsi2sd xmm1, dword ptr [rip + 0x6ef8]"),
    (0x1150, "f2480f2aca", "cvtsi2sd xmm1, rdx"),
    (0x1160, "f24d0f2acc", "cvtsi2sd xmm9, r12"),
    (0x1170, "f2480f2adb", "cvtsi2sd xmm3, rbx"),
    (0x1180, "f2480f2a0b", "cvtsi2sd xmm1, qword ptr [rbx]"),
    (0x1190, "f24c0f2a5310", "cvtsi2sd xmm10, qword ptr [rbx + 0x10]"),
    (0x11a0, "f2480f2a548820", "cvtsi2sd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x11b0, "f2480f2a0d876e0000", "cvtsi2sd xmm1, qword ptr [rip + 0x6e87]"),
]
