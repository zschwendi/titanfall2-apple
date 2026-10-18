"""Hand-written test vectors of the sse_psub family: psubb, psubw, psubd, psubq, packed wrapping
subtractions with an xmm destination and an xmm register, a 16-byte memory operand (register-based
and RIP-relative) as the source; a register with itself, and register 8 and above.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660ff8ca", "psubb xmm1, xmm2"),
    (0x1010, "66450ff8cc", "psubb xmm9, xmm12"),
    (0x1020, "660ff8db", "psubb xmm3, xmm3"),
    (0x1030, "660ff80b", "psubb xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440ff85310", "psubb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660ff8548820", "psubb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660ff80dd86f0000", "psubb xmm1, xmmword ptr [rip + 0x6fd8]"),
    (0x1100, "660ff9ca", "psubw xmm1, xmm2"),
    (0x1110, "66450ff9cc", "psubw xmm9, xmm12"),
    (0x1120, "660ff9db", "psubw xmm3, xmm3"),
    (0x1130, "660ff90b", "psubw xmm1, xmmword ptr [rbx]"),
    (0x1140, "66440ff95310", "psubw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "660ff9548820", "psubw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "660ff90dd86e0000", "psubw xmm1, xmmword ptr [rip + 0x6ed8]"),
    (0x1200, "660ffaca", "psubd xmm1, xmm2"),
    (0x1210, "66450ffacc", "psubd xmm9, xmm12"),
    (0x1220, "660ffadb", "psubd xmm3, xmm3"),
    (0x1230, "660ffa0b", "psubd xmm1, xmmword ptr [rbx]"),
    (0x1240, "66440ffa5310", "psubd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "660ffa548820", "psubd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "660ffa0dd86d0000", "psubd xmm1, xmmword ptr [rip + 0x6dd8]"),
    (0x1300, "660ffbca", "psubq xmm1, xmm2"),
    (0x1310, "66450ffbcc", "psubq xmm9, xmm12"),
    (0x1320, "660ffbdb", "psubq xmm3, xmm3"),
    (0x1330, "660ffb0b", "psubq xmm1, xmmword ptr [rbx]"),
    (0x1340, "66440ffb5310", "psubq xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1350, "660ffb548820", "psubq xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "660ffb0dd86c0000", "psubq xmm1, xmmword ptr [rip + 0x6cd8]"),
]
