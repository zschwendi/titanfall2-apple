"""Hand-written test vectors of the sse_cmppd family: cmpeqpd, cmpltpd, cmplepd, cmpunordpd, cmpneqpd, cmpnltpd, cmpnlepd, cmpordpd.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

A 16-byte memory operand must be 16-byte aligned (#GP otherwise).  The harness places a
register-relative operand itself: aligned in four states of five, misaligned in the fifth.  A
RIP-relative operand lies where the encoding puts it, so both kinds are written out here.
"""

VECTORS = [
    (0x1000, "660fc2ca00", "cmpeqpd xmm1, xmm2"),
    (0x1010, "66450fc2cc00", "cmpeqpd xmm9, xmm12"),
    (0x1020, "660fc2db00", "cmpeqpd xmm3, xmm3"),
    (0x1030, "660fc20b00", "cmpeqpd xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440fc2531000", "cmpeqpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660fc254882000", "cmpeqpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660fc20dd76f000000", "cmpeqpd xmm1, xmmword ptr [rip + 0x6fd7]"),      # the operand is at 0x8040: aligned
    (0x1100, "660fc2ca01", "cmpltpd xmm1, xmm2"),
    (0x1110, "66450fc2cc01", "cmpltpd xmm9, xmm12"),
    (0x1120, "660fc2db01", "cmpltpd xmm3, xmm3"),
    (0x1130, "660fc20b01", "cmpltpd xmm1, xmmword ptr [rbx]"),
    (0x1140, "66440fc2531001", "cmpltpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "660fc254882001", "cmpltpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "660fc20dd76e000001", "cmpltpd xmm1, xmmword ptr [rip + 0x6ed7]"),      # the operand is at 0x8040: aligned
    (0x1200, "660fc2ca02", "cmplepd xmm1, xmm2"),
    (0x1210, "66450fc2cc02", "cmplepd xmm9, xmm12"),
    (0x1220, "660fc2db02", "cmplepd xmm3, xmm3"),
    (0x1230, "660fc20b02", "cmplepd xmm1, xmmword ptr [rbx]"),
    (0x1240, "66440fc2531002", "cmplepd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "660fc254882002", "cmplepd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "660fc20dd76d000002", "cmplepd xmm1, xmmword ptr [rip + 0x6dd7]"),      # the operand is at 0x8040: aligned
    (0x1300, "660fc2ca03", "cmpunordpd xmm1, xmm2"),
    (0x1310, "66450fc2cc03", "cmpunordpd xmm9, xmm12"),
    (0x1320, "660fc2db03", "cmpunordpd xmm3, xmm3"),
    (0x1330, "660fc20b03", "cmpunordpd xmm1, xmmword ptr [rbx]"),
    (0x1340, "66440fc2531003", "cmpunordpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1350, "660fc254882003", "cmpunordpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "660fc20dd76c000003", "cmpunordpd xmm1, xmmword ptr [rip + 0x6cd7]"),      # the operand is at 0x8040: aligned
    (0x1400, "660fc2ca04", "cmpneqpd xmm1, xmm2"),
    (0x1410, "66450fc2cc04", "cmpneqpd xmm9, xmm12"),
    (0x1420, "660fc2db04", "cmpneqpd xmm3, xmm3"),
    (0x1430, "660fc20b04", "cmpneqpd xmm1, xmmword ptr [rbx]"),
    (0x1440, "66440fc2531004", "cmpneqpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1450, "660fc254882004", "cmpneqpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1460, "660fc20dd76b000004", "cmpneqpd xmm1, xmmword ptr [rip + 0x6bd7]"),      # the operand is at 0x8040: aligned
    (0x1500, "660fc2ca05", "cmpnltpd xmm1, xmm2"),
    (0x1510, "66450fc2cc05", "cmpnltpd xmm9, xmm12"),
    (0x1520, "660fc2db05", "cmpnltpd xmm3, xmm3"),
    (0x1530, "660fc20b05", "cmpnltpd xmm1, xmmword ptr [rbx]"),
    (0x1540, "66440fc2531005", "cmpnltpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1550, "660fc254882005", "cmpnltpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1560, "660fc20dd76a000005", "cmpnltpd xmm1, xmmword ptr [rip + 0x6ad7]"),      # the operand is at 0x8040: aligned
    (0x1600, "660fc2ca06", "cmpnlepd xmm1, xmm2"),
    (0x1610, "66450fc2cc06", "cmpnlepd xmm9, xmm12"),
    (0x1620, "660fc2db06", "cmpnlepd xmm3, xmm3"),
    (0x1630, "660fc20b06", "cmpnlepd xmm1, xmmword ptr [rbx]"),
    (0x1640, "66440fc2531006", "cmpnlepd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1650, "660fc254882006", "cmpnlepd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1660, "660fc20dd769000006", "cmpnlepd xmm1, xmmword ptr [rip + 0x69d7]"),      # the operand is at 0x8040: aligned
    (0x1700, "660fc2ca07", "cmpordpd xmm1, xmm2"),
    (0x1710, "66450fc2cc07", "cmpordpd xmm9, xmm12"),
    (0x1720, "660fc2db07", "cmpordpd xmm3, xmm3"),
    (0x1730, "660fc20b07", "cmpordpd xmm1, xmmword ptr [rbx]"),
    (0x1740, "66440fc2531007", "cmpordpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1750, "660fc254882007", "cmpordpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1760, "660fc20dd768000007", "cmpordpd xmm1, xmmword ptr [rip + 0x68d7]"),      # the operand is at 0x8040: aligned
]
