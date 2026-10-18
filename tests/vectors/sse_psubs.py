"""Hand-written test vectors of the sse_psubs family: psubsb, psubsw, psubusb and psubusw, each
with an xmm,xmm form (distinct registers, the same register twice, and the register-8-or-above
form), an xmm,m128 form (plain base, base plus displacement, base plus index), and an xmm,m128rip
form whose data is at 0x8040.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660fe8ca", "psubsb xmm1, xmm2"),
    (0x1010, "66450fe8cc", "psubsb xmm9, xmm12"),
    (0x1020, "660fe8db", "psubsb xmm3, xmm3"),
    (0x1030, "660fe80b", "psubsb xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440fe85310", "psubsb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660fe8548820", "psubsb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660fe80dd86f0000", "psubsb xmm1, xmmword ptr [rip + 0x6fd8]"),
    (0x1070, "660fe9ca", "psubsw xmm1, xmm2"),
    (0x1080, "66450fe9cc", "psubsw xmm9, xmm12"),
    (0x1090, "660fe9db", "psubsw xmm3, xmm3"),
    (0x10a0, "660fe90b", "psubsw xmm1, xmmword ptr [rbx]"),
    (0x10b0, "66440fe95310", "psubsw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x10c0, "660fe9548820", "psubsw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "660fe90d686f0000", "psubsw xmm1, xmmword ptr [rip + 0x6f68]"),
    (0x10e0, "660fd8ca", "psubusb xmm1, xmm2"),
    (0x10f0, "66450fd8cc", "psubusb xmm9, xmm12"),
    (0x1100, "660fd8db", "psubusb xmm3, xmm3"),
    (0x1110, "660fd80b", "psubusb xmm1, xmmword ptr [rbx]"),
    (0x1120, "66440fd85310", "psubusb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1130, "660fd8548820", "psubusb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1140, "660fd80df86e0000", "psubusb xmm1, xmmword ptr [rip + 0x6ef8]"),
    (0x1150, "660fd9ca", "psubusw xmm1, xmm2"),
    (0x1160, "66450fd9cc", "psubusw xmm9, xmm12"),
    (0x1170, "660fd9db", "psubusw xmm3, xmm3"),
    (0x1180, "660fd90b", "psubusw xmm1, xmmword ptr [rbx]"),
    (0x1190, "66440fd95310", "psubusw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x11a0, "660fd9548820", "psubusw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x11b0, "660fd90d886e0000", "psubusw xmm1, xmmword ptr [rip + 0x6e88]"),
]
