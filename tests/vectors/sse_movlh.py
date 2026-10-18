"""Hand-written test vectors of the sse_movlh family: movlhps and movhlps, register forms only.
Each mnemonic has the xmm, xmm form with two low registers, the form with a register numbered
8 or above (hi), and a register with itself.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "0f16ca", "movlhps xmm1, xmm2"),
    (0x1010, "450f16cc", "movlhps xmm9, xmm12"),
    (0x1020, "0f16db", "movlhps xmm3, xmm3"),
    (0x1030, "410f16d4", "movlhps xmm2, xmm12"),
    (0x1040, "440f16e2", "movlhps xmm12, xmm2"),
    (0x1050, "450f16ff", "movlhps xmm15, xmm15"),
    (0x1060, "0f12ca", "movhlps xmm1, xmm2"),
    (0x1070, "450f12cc", "movhlps xmm9, xmm12"),
    (0x1080, "0f12db", "movhlps xmm3, xmm3"),
    (0x1090, "410f12d4", "movhlps xmm2, xmm12"),
    (0x10a0, "440f12e2", "movhlps xmm12, xmm2"),
    (0x10b0, "450f12ff", "movhlps xmm15, xmm15"),
]
