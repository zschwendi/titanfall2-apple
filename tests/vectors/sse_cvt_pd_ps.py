"""Hand-written test vectors of the sse_cvt_pd_ps family: cvtps2pd, cvtpd2ps.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

cvtps2pd reads 8 bytes from memory, which has no alignment rule.  cvtpd2ps has a register source
only: its 16-byte memory form is not in the game and is refused by the translator.
"""

VECTORS = [
    (0x1000, "0f5aca", "cvtps2pd xmm1, xmm2"),
    (0x1010, "450f5acc", "cvtps2pd xmm9, xmm12"),
    (0x1020, "0f5adb", "cvtps2pd xmm3, xmm3"),
    (0x1030, "0f5a0b", "cvtps2pd xmm1, qword ptr [rbx]"),
    (0x1040, "440f5a5310", "cvtps2pd xmm10, qword ptr [rbx + 0x10]"),
    (0x1050, "0f5a548820", "cvtps2pd xmm2, qword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "0f5a0dd96f0000", "cvtps2pd xmm1, qword ptr [rip + 0x6fd9]"),      # the operand is at 0x8040
    (0x1070, "660f5aca", "cvtpd2ps xmm1, xmm2"),
    (0x1080, "66450f5acc", "cvtpd2ps xmm9, xmm12"),
    (0x1090, "660f5adb", "cvtpd2ps xmm3, xmm3"),
]

# cvtpd2ps with a 16-byte memory source: not in the game, not translated.
REFUSED = [
    (0x1100, "660f5a0b", "cvtpd2ps xmm1, xmmword ptr [rbx]", "memory source"),
]
