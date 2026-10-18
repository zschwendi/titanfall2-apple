"""Hand-written test vectors of the sse_pshufb family: pshufb (66 0F 38 00 /r), the byte shuffle
of an xmm register under control bytes from an xmm register or 16 bytes of memory: register with
itself, the hi registers 8 to 15 as destination, source and both, and the memory forms register
indirect, base plus displacement, SIB and RIP-relative (its data is at 0x8040, aligned). The MMX
form without the 66 prefix is refused.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660f3800ca", "pshufb xmm1, xmm2"),
    (0x1010, "66450f3800cc", "pshufb xmm9, xmm12"),
    (0x1020, "660f3800db", "pshufb xmm3, xmm3"),
    (0x1030, "660f38000b", "pshufb xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440f38005310", "pshufb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660f3800548820", "pshufb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660f38000dd76f0000", "pshufb xmm1, xmmword ptr [rip + 0x6fd7]"),
    (0x1070, "66440f3800e1", "pshufb xmm12, xmm1"),
    (0x1080, "66450f3800e4", "pshufb xmm12, xmm12"),
]

REFUSED = [
    (0x1090, "0f3800ca", "pshufb mm1, mm2", "pshufb"),
]
