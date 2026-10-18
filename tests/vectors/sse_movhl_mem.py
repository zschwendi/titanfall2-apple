"""Hand-written test vectors of the sse_movhl_mem family: movhps, movlps, movhpd and movlpd, each
in both directions (load and store) with the forms x,m64, m64,x and x,m64rip, and with xmm10
(a register numbered 8 or above) and the memory operands [rbx], [rbx + 0x10], an index form and
RIP-relative data at 0x8040.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "0f160b", "movhps xmm1, qword ptr [rbx]"),
    (0x1010, "440f165310", "movhps xmm10, qword ptr [rbx + 0x10]"),
    (0x1020, "0f16548820", "movhps xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1030, "0f160d09700000", "movhps xmm1, qword ptr [rip + 0x7009]"),
    (0x1040, "0f170b", "movhps qword ptr [rbx], xmm1"),
    (0x1050, "440f175310", "movhps qword ptr [rbx + 0x10], xmm10"),
    (0x1060, "0f17548820", "movhps qword ptr [rax + rcx*4 + 0x20], xmm2"),
    (0x1070, "0f170dc96f0000", "movhps qword ptr [rip + 0x6fc9], xmm1"),
    (0x1080, "0f120b", "movlps xmm1, qword ptr [rbx]"),
    (0x1090, "440f125310", "movlps xmm10, qword ptr [rbx + 0x10]"),
    (0x10a0, "0f12548820", "movlps xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x10b0, "0f120d896f0000", "movlps xmm1, qword ptr [rip + 0x6f89]"),
    (0x10c0, "0f130b", "movlps qword ptr [rbx], xmm1"),
    (0x10d0, "440f135310", "movlps qword ptr [rbx + 0x10], xmm10"),
    (0x10e0, "0f13548820", "movlps qword ptr [rax + rcx*4 + 0x20], xmm2"),
    (0x10f0, "0f130d496f0000", "movlps qword ptr [rip + 0x6f49], xmm1"),
    (0x1100, "660f160b", "movhpd xmm1, qword ptr [rbx]"),
    (0x1110, "66440f165310", "movhpd xmm10, qword ptr [rbx + 0x10]"),
    (0x1120, "660f16548820", "movhpd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1130, "660f160d086f0000", "movhpd xmm1, qword ptr [rip + 0x6f08]"),
    (0x1140, "660f170b", "movhpd qword ptr [rbx], xmm1"),
    (0x1150, "66440f175310", "movhpd qword ptr [rbx + 0x10], xmm10"),
    (0x1160, "660f17548820", "movhpd qword ptr [rax + rcx*4 + 0x20], xmm2"),
    (0x1170, "660f170dc86e0000", "movhpd qword ptr [rip + 0x6ec8], xmm1"),
    (0x1180, "660f120b", "movlpd xmm1, qword ptr [rbx]"),
    (0x1190, "66440f125310", "movlpd xmm10, qword ptr [rbx + 0x10]"),
    (0x11a0, "660f12548820", "movlpd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x11b0, "660f120d886e0000", "movlpd xmm1, qword ptr [rip + 0x6e88]"),
    (0x11c0, "660f130b", "movlpd qword ptr [rbx], xmm1"),
    (0x11d0, "66440f135310", "movlpd qword ptr [rbx + 0x10], xmm10"),
    (0x11e0, "660f13548820", "movlpd qword ptr [rax + rcx*4 + 0x20], xmm2"),
    (0x11f0, "660f130d486e0000", "movlpd qword ptr [rip + 0x6e48], xmm1"),
]
