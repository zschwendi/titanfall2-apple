"""Hand-written test vectors of the sse_pextr family: pextrw (66 0F C5 and 66 0F 3A 15), pextrd and
pextrq (66 0F 3A 16, with and without REX.W), with the register destinations of all three, the
memory destination of pextrd, and the hi form (xmm 8 and above, r8d and above), with immediates
0, 1, 2, 3, 4, 7, 8, 0xff (every lane, and bits above the lane index).

REFUSED: the memory forms of pextrw (m16) and pextrq (m64), which no instance of the game or the
corpus has, so they cannot be tested.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660fc5ca00", "pextrw ecx, xmm2, 0"),
    (0x1010, "660fc5ca01", "pextrw ecx, xmm2, 1"),
    (0x1020, "660fc5ca07", "pextrw ecx, xmm2, 7"),
    (0x1030, "660fc5ca08", "pextrw ecx, xmm2, 8"),
    (0x1040, "660fc5caff", "pextrw ecx, xmm2, 0xff"),
    (0x1050, "66450fc5cc00", "pextrw r9d, xmm12, 0"),
    (0x1060, "66450fc5cc01", "pextrw r9d, xmm12, 1"),
    (0x1070, "66450fc5cc07", "pextrw r9d, xmm12, 7"),
    (0x1080, "66450fc5cc08", "pextrw r9d, xmm12, 8"),
    (0x1090, "66450fc5ccff", "pextrw r9d, xmm12, 0xff"),
    (0x10a0, "660f3a15ca00", "pextrw edx, xmm1, 0"),
    (0x10b0, "660f3a15ca01", "pextrw edx, xmm1, 1"),
    (0x10c0, "660f3a15ca07", "pextrw edx, xmm1, 7"),
    (0x10d0, "660f3a15ca08", "pextrw edx, xmm1, 8"),
    (0x10e0, "660f3a15caff", "pextrw edx, xmm1, 0xff"),
    (0x10f0, "66450f3a15cc00", "pextrw r12d, xmm9, 0"),
    (0x1100, "66450f3a15cc01", "pextrw r12d, xmm9, 1"),
    (0x1110, "66450f3a15cc07", "pextrw r12d, xmm9, 7"),
    (0x1120, "66450f3a15cc08", "pextrw r12d, xmm9, 8"),
    (0x1130, "66450f3a15ccff", "pextrw r12d, xmm9, 0xff"),
    (0x1140, "660f3a16ca00", "pextrd edx, xmm1, 0"),
    (0x1150, "660f3a16ca01", "pextrd edx, xmm1, 1"),
    (0x1160, "660f3a16ca03", "pextrd edx, xmm1, 3"),
    (0x1170, "660f3a16ca04", "pextrd edx, xmm1, 4"),
    (0x1180, "660f3a16caff", "pextrd edx, xmm1, 0xff"),
    (0x1190, "66450f3a16cc00", "pextrd r12d, xmm9, 0"),
    (0x11a0, "66450f3a16cc01", "pextrd r12d, xmm9, 1"),
    (0x11b0, "66450f3a16cc03", "pextrd r12d, xmm9, 3"),
    (0x11c0, "66450f3a16cc04", "pextrd r12d, xmm9, 4"),
    (0x11d0, "66450f3a16ccff", "pextrd r12d, xmm9, 0xff"),
    (0x11e0, "660f3a160b00", "pextrd dword ptr [rbx], xmm1, 0"),
    (0x11f0, "660f3a160b01", "pextrd dword ptr [rbx], xmm1, 1"),
    (0x1200, "660f3a160b03", "pextrd dword ptr [rbx], xmm1, 3"),
    (0x1210, "660f3a160b04", "pextrd dword ptr [rbx], xmm1, 4"),
    (0x1220, "660f3a160bff", "pextrd dword ptr [rbx], xmm1, 0xff"),
    (0x1230, "66480f3a16ca00", "pextrq rdx, xmm1, 0"),
    (0x1240, "66480f3a16ca01", "pextrq rdx, xmm1, 1"),
    (0x1250, "66480f3a16ca02", "pextrq rdx, xmm1, 2"),
    (0x1260, "66480f3a16caff", "pextrq rdx, xmm1, 0xff"),
    (0x1270, "664d0f3a16cc00", "pextrq r12, xmm9, 0"),
    (0x1280, "664d0f3a16cc01", "pextrq r12, xmm9, 1"),
    (0x1290, "664d0f3a16cc02", "pextrq r12, xmm9, 2"),
    (0x12a0, "664d0f3a16ccff", "pextrq r12, xmm9, 0xff"),
]

REFUSED = [
    (0x12b0, "660f3a150b00", "pextrw word ptr [rbx], xmm1, 0", "pextrw"),
    (0x12c0, "660f3a150bff", "pextrw word ptr [rbx], xmm1, 0xff", "pextrw"),
    (0x12d0, "66480f3a160b00", "pextrq qword ptr [rbx], xmm1, 0", "pextrq"),
    (0x12e0, "66480f3a160bff", "pextrq qword ptr [rbx], xmm1, 0xff", "pextrq"),
]
