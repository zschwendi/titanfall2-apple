"""Hand-written test vectors of the sse_cmpss family: cmpeqss, cmpltss, cmpless, cmpunordss, cmpneqss, cmpnltss, cmpnless, cmpordss.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    # cmpeqss
    (0x1000, "f30fc2ca00", "cmpeqss xmm1, xmm2"),
    (0x1010, "f3450fc2cc00", "cmpeqss xmm9, xmm12"),
    (0x1020, "f30fc2db00", "cmpeqss xmm3, xmm3"),
    (0x1030, "f30fc20b00", "cmpeqss xmm1, dword ptr [rbx]"),
    (0x1040, "f3440fc2531000", "cmpeqss xmm10, dword ptr [rbx + 0x10]"),
    (0x1050, "f30fc254882000", "cmpeqss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f30fc20dd76f000000", "cmpeqss xmm1, dword ptr [rip + 0x6fd7]"),
    # cmpltss
    (0x1100, "f30fc2ca01", "cmpltss xmm1, xmm2"),
    (0x1110, "f3450fc2cc01", "cmpltss xmm9, xmm12"),
    (0x1120, "f30fc2db01", "cmpltss xmm3, xmm3"),
    (0x1130, "f30fc20b01", "cmpltss xmm1, dword ptr [rbx]"),
    (0x1140, "f3440fc2531001", "cmpltss xmm10, dword ptr [rbx + 0x10]"),
    (0x1150, "f30fc254882001", "cmpltss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "f30fc20dd76e000001", "cmpltss xmm1, dword ptr [rip + 0x6ed7]"),
    # cmpless
    (0x1200, "f30fc2ca02", "cmpless xmm1, xmm2"),
    (0x1210, "f3450fc2cc02", "cmpless xmm9, xmm12"),
    (0x1220, "f30fc2db02", "cmpless xmm3, xmm3"),
    (0x1230, "f30fc20b02", "cmpless xmm1, dword ptr [rbx]"),
    (0x1240, "f3440fc2531002", "cmpless xmm10, dword ptr [rbx + 0x10]"),
    (0x1250, "f30fc254882002", "cmpless xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "f30fc20dd76d000002", "cmpless xmm1, dword ptr [rip + 0x6dd7]"),
    # cmpunordss
    (0x1300, "f30fc2ca03", "cmpunordss xmm1, xmm2"),
    (0x1310, "f3450fc2cc03", "cmpunordss xmm9, xmm12"),
    (0x1320, "f30fc2db03", "cmpunordss xmm3, xmm3"),
    (0x1330, "f30fc20b03", "cmpunordss xmm1, dword ptr [rbx]"),
    (0x1340, "f3440fc2531003", "cmpunordss xmm10, dword ptr [rbx + 0x10]"),
    (0x1350, "f30fc254882003", "cmpunordss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "f30fc20dd76c000003", "cmpunordss xmm1, dword ptr [rip + 0x6cd7]"),
    # cmpneqss
    (0x1400, "f30fc2ca04", "cmpneqss xmm1, xmm2"),
    (0x1410, "f3450fc2cc04", "cmpneqss xmm9, xmm12"),
    (0x1420, "f30fc2db04", "cmpneqss xmm3, xmm3"),
    (0x1430, "f30fc20b04", "cmpneqss xmm1, dword ptr [rbx]"),
    (0x1440, "f3440fc2531004", "cmpneqss xmm10, dword ptr [rbx + 0x10]"),
    (0x1450, "f30fc254882004", "cmpneqss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1460, "f30fc20dd76b000004", "cmpneqss xmm1, dword ptr [rip + 0x6bd7]"),
    # cmpnltss
    (0x1500, "f30fc2ca05", "cmpnltss xmm1, xmm2"),
    (0x1510, "f3450fc2cc05", "cmpnltss xmm9, xmm12"),
    (0x1520, "f30fc2db05", "cmpnltss xmm3, xmm3"),
    (0x1530, "f30fc20b05", "cmpnltss xmm1, dword ptr [rbx]"),
    (0x1540, "f3440fc2531005", "cmpnltss xmm10, dword ptr [rbx + 0x10]"),
    (0x1550, "f30fc254882005", "cmpnltss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1560, "f30fc20dd76a000005", "cmpnltss xmm1, dword ptr [rip + 0x6ad7]"),
    # cmpnless
    (0x1600, "f30fc2ca06", "cmpnless xmm1, xmm2"),
    (0x1610, "f3450fc2cc06", "cmpnless xmm9, xmm12"),
    (0x1620, "f30fc2db06", "cmpnless xmm3, xmm3"),
    (0x1630, "f30fc20b06", "cmpnless xmm1, dword ptr [rbx]"),
    (0x1640, "f3440fc2531006", "cmpnless xmm10, dword ptr [rbx + 0x10]"),
    (0x1650, "f30fc254882006", "cmpnless xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1660, "f30fc20dd769000006", "cmpnless xmm1, dword ptr [rip + 0x69d7]"),
    # cmpordss
    (0x1700, "f30fc2ca07", "cmpordss xmm1, xmm2"),
    (0x1710, "f3450fc2cc07", "cmpordss xmm9, xmm12"),
    (0x1720, "f30fc2db07", "cmpordss xmm3, xmm3"),
    (0x1730, "f30fc20b07", "cmpordss xmm1, dword ptr [rbx]"),
    (0x1740, "f3440fc2531007", "cmpordss xmm10, dword ptr [rbx + 0x10]"),
    (0x1750, "f30fc254882007", "cmpordss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1760, "f30fc20dd768000007", "cmpordss xmm1, dword ptr [rip + 0x68d7]"),
]
