"""Hand-written test vectors of the sse_cvtt_s2si family: cvttss2si, cvttsd2si.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f30f2cca", "cvttss2si ecx, xmm2"),
    (0x1010, "f3450f2ccc", "cvttss2si r9d, xmm12"),
    (0x1020, "f30f2cdb", "cvttss2si ebx, xmm3"),
    (0x1030, "f30f2c0b", "cvttss2si ecx, dword ptr [rbx]"),
    (0x1040, "f3440f2c5310", "cvttss2si r10d, dword ptr [rbx + 0x10]"),
    (0x1050, "f30f2c548820", "cvttss2si edx, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f30f2c0dd86f0000", "cvttss2si ecx, dword ptr [rip + 0x6fd8]"),
    (0x1100, "f3480f2cca", "cvttss2si rcx, xmm2"),
    (0x1110, "f34d0f2ccc", "cvttss2si r9, xmm12"),
    (0x1120, "f3480f2cdb", "cvttss2si rbx, xmm3"),
    (0x1130, "f3480f2c0b", "cvttss2si rcx, dword ptr [rbx]"),
    (0x1140, "f34c0f2c5310", "cvttss2si r10, dword ptr [rbx + 0x10]"),
    (0x1150, "f3480f2c548820", "cvttss2si rdx, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "f3480f2c0dd76e0000", "cvttss2si rcx, dword ptr [rip + 0x6ed7]"),
    (0x1200, "f20f2cca", "cvttsd2si ecx, xmm2"),
    (0x1210, "f2450f2ccc", "cvttsd2si r9d, xmm12"),
    (0x1220, "f20f2cdb", "cvttsd2si ebx, xmm3"),
    (0x1230, "f20f2c0b", "cvttsd2si ecx, qword ptr [rbx]"),
    (0x1240, "f2440f2c5310", "cvttsd2si r10d, qword ptr [rbx + 0x10]"),
    (0x1250, "f20f2c548820", "cvttsd2si edx, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "f20f2c0dd86d0000", "cvttsd2si ecx, qword ptr [rip + 0x6dd8]"),
    (0x1300, "f2480f2cca", "cvttsd2si rcx, xmm2"),
    (0x1310, "f24d0f2ccc", "cvttsd2si r9, xmm12"),
    (0x1320, "f2480f2cdb", "cvttsd2si rbx, xmm3"),
    (0x1330, "f2480f2c0b", "cvttsd2si rcx, qword ptr [rbx]"),
    (0x1340, "f24c0f2c5310", "cvttsd2si r10, qword ptr [rbx + 0x10]"),
    (0x1350, "f2480f2c548820", "cvttsd2si rdx, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "f2480f2c0dd76c0000", "cvttsd2si rcx, qword ptr [rip + 0x6cd7]"),
]
