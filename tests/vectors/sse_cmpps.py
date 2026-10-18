"""Hand-written test vectors of the sse_cmpps family: cmpeqps, cmpltps, cmpleps, cmpunordps, cmpneqps, cmpnltps, cmpnleps, cmpordps.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

A 16-byte memory operand must be 16-byte aligned (#GP otherwise).  The harness places a
register-relative operand itself: aligned in four states of five, misaligned in the fifth.  A
RIP-relative operand lies where the encoding puts it, so it is written out here.
"""

VECTORS = [
    (0x1000, "0fc2ca00", "cmpeqps xmm1, xmm2"),
    (0x1010, "450fc2cc00", "cmpeqps xmm9, xmm12"),
    (0x1020, "0fc2db00", "cmpeqps xmm3, xmm3"),
    (0x1030, "0fc20b00", "cmpeqps xmm1, xmmword ptr [rbx]"),
    (0x1040, "440fc2531000", "cmpeqps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "0fc254882000", "cmpeqps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "0fc20dd86f000000", "cmpeqps xmm1, xmmword ptr [rip + 0x6fd8]"),      # the operand is at 0x8040: aligned
    (0x1070, "0fc20dd06f000000", "cmpeqps xmm1, xmmword ptr [rip + 0x6fd0]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x1100, "0fc2ca01", "cmpltps xmm1, xmm2"),
    (0x1110, "450fc2cc01", "cmpltps xmm9, xmm12"),
    (0x1120, "0fc2db01", "cmpltps xmm3, xmm3"),
    (0x1130, "0fc20b01", "cmpltps xmm1, xmmword ptr [rbx]"),
    (0x1140, "440fc2531001", "cmpltps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "0fc254882001", "cmpltps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "0fc20dd86e000001", "cmpltps xmm1, xmmword ptr [rip + 0x6ed8]"),      # the operand is at 0x8040: aligned
    (0x1170, "0fc20dd06e000001", "cmpltps xmm1, xmmword ptr [rip + 0x6ed0]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x1200, "0fc2ca02", "cmpleps xmm1, xmm2"),
    (0x1210, "450fc2cc02", "cmpleps xmm9, xmm12"),
    (0x1220, "0fc2db02", "cmpleps xmm3, xmm3"),
    (0x1230, "0fc20b02", "cmpleps xmm1, xmmword ptr [rbx]"),
    (0x1240, "440fc2531002", "cmpleps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "0fc254882002", "cmpleps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "0fc20dd86d000002", "cmpleps xmm1, xmmword ptr [rip + 0x6dd8]"),      # the operand is at 0x8040: aligned
    (0x1270, "0fc20dd06d000002", "cmpleps xmm1, xmmword ptr [rip + 0x6dd0]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x1300, "0fc2ca03", "cmpunordps xmm1, xmm2"),
    (0x1310, "450fc2cc03", "cmpunordps xmm9, xmm12"),
    (0x1320, "0fc2db03", "cmpunordps xmm3, xmm3"),
    (0x1330, "0fc20b03", "cmpunordps xmm1, xmmword ptr [rbx]"),
    (0x1340, "440fc2531003", "cmpunordps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1350, "0fc254882003", "cmpunordps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "0fc20dd86c000003", "cmpunordps xmm1, xmmword ptr [rip + 0x6cd8]"),      # the operand is at 0x8040: aligned
    (0x1400, "0fc2ca04", "cmpneqps xmm1, xmm2"),
    (0x1410, "450fc2cc04", "cmpneqps xmm9, xmm12"),
    (0x1420, "0fc2db04", "cmpneqps xmm3, xmm3"),
    (0x1430, "0fc20b04", "cmpneqps xmm1, xmmword ptr [rbx]"),
    (0x1440, "440fc2531004", "cmpneqps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1450, "0fc254882004", "cmpneqps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1460, "0fc20dd86b000004", "cmpneqps xmm1, xmmword ptr [rip + 0x6bd8]"),      # the operand is at 0x8040: aligned
    (0x1470, "0fc20dd06b000004", "cmpneqps xmm1, xmmword ptr [rip + 0x6bd0]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x1500, "0fc2ca05", "cmpnltps xmm1, xmm2"),
    (0x1510, "450fc2cc05", "cmpnltps xmm9, xmm12"),
    (0x1520, "0fc2db05", "cmpnltps xmm3, xmm3"),
    (0x1530, "0fc20b05", "cmpnltps xmm1, xmmword ptr [rbx]"),
    (0x1540, "440fc2531005", "cmpnltps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1550, "0fc254882005", "cmpnltps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1560, "0fc20dd86a000005", "cmpnltps xmm1, xmmword ptr [rip + 0x6ad8]"),      # the operand is at 0x8040: aligned
    (0x1600, "0fc2ca06", "cmpnleps xmm1, xmm2"),
    (0x1610, "450fc2cc06", "cmpnleps xmm9, xmm12"),
    (0x1620, "0fc2db06", "cmpnleps xmm3, xmm3"),
    (0x1630, "0fc20b06", "cmpnleps xmm1, xmmword ptr [rbx]"),
    (0x1640, "440fc2531006", "cmpnleps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1650, "0fc254882006", "cmpnleps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1660, "0fc20dd869000006", "cmpnleps xmm1, xmmword ptr [rip + 0x69d8]"),      # the operand is at 0x8040: aligned
    (0x1670, "0fc20dd069000006", "cmpnleps xmm1, xmmword ptr [rip + 0x69d0]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x1700, "0fc2ca07", "cmpordps xmm1, xmm2"),
    (0x1710, "450fc2cc07", "cmpordps xmm9, xmm12"),
    (0x1720, "0fc2db07", "cmpordps xmm3, xmm3"),
    (0x1730, "0fc20b07", "cmpordps xmm1, xmmword ptr [rbx]"),
    (0x1740, "440fc2531007", "cmpordps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1750, "0fc254882007", "cmpordps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1760, "0fc20dd868000007", "cmpordps xmm1, xmmword ptr [rip + 0x68d8]"),      # the operand is at 0x8040: aligned
]
