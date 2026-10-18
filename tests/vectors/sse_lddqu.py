"""Hand-written test vectors of the sse_lddqu family: lddqu (load), movntps and movntdq (stores),
each with a register-relative operand, an indexed one, a high register (xmm10) and a RIP-relative
operand whose data is at the aligned address 0x8040.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "f20ff00b", "lddqu xmm1, xmmword ptr [rbx]"),
    (0x1010, "f2440ff05310", "lddqu xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1020, "f20ff0548820", "lddqu xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1030, "f20ff00d08700000", "lddqu xmm1, xmmword ptr [rip + 0x7008]"),
    (0x1040, "0f2b0b", "movntps xmmword ptr [rbx], xmm1"),
    (0x1050, "440f2b5310", "movntps xmmword ptr [rbx + 0x10], xmm10"),
    (0x1060, "0f2b548820", "movntps xmmword ptr [rax + rcx*4 + 0x20], xmm2"),
    (0x1070, "0f2b0dc96f0000", "movntps xmmword ptr [rip + 0x6fc9], xmm1"),
    (0x1080, "660fe70b", "movntdq xmmword ptr [rbx], xmm1"),
    (0x1090, "66440fe75310", "movntdq xmmword ptr [rbx + 0x10], xmm10"),
    (0x10a0, "660fe7548820", "movntdq xmmword ptr [rax + rcx*4 + 0x20], xmm2"),
    (0x10b0, "660fe70d886f0000", "movntdq xmmword ptr [rip + 0x6f88], xmm1"),
]
