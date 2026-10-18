"""Hand-written test vectors of the sse_cvtp2dq family: cvtps2dq, cvttps2dq, cvtpd2dq, cvttpd2dq.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

Every instance in the game and the corpus is register-to-register (x,x), so these vectors are too:
a register pair, a register with itself, and one register numbered 8 or above (the form hi).
"""

VECTORS = [
    (0x1000, "f30f5bca", "cvttps2dq xmm1, xmm2"),
    (0x1010, "f3450f5bcc", "cvttps2dq xmm9, xmm12"),
    (0x1020, "f30f5bdb", "cvttps2dq xmm3, xmm3"),
    (0x1030, "660fe6ca", "cvttpd2dq xmm1, xmm2"),
    (0x1040, "66450fe6cc", "cvttpd2dq xmm9, xmm12"),
    (0x1050, "660fe6db", "cvttpd2dq xmm3, xmm3"),
    (0x1060, "660f5bca", "cvtps2dq xmm1, xmm2"),
    (0x1070, "66450f5bcc", "cvtps2dq xmm9, xmm12"),
    (0x1080, "660f5bdb", "cvtps2dq xmm3, xmm3"),
    (0x1090, "f20fe6ca", "cvtpd2dq xmm1, xmm2"),
    (0x10a0, "f2450fe6cc", "cvtpd2dq xmm9, xmm12"),
    (0x10b0, "f20fe6db", "cvtpd2dq xmm3, xmm3"),
]
