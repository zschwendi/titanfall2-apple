"""Hand-written test vectors of the sse_shift_dq family: psrldq and pslldq, whole-register byte
shifts by an immediate, with counts 0, 1, 5, 8, 0xb, 0xf, 0x10, 0x80 and 0xff, and with
register 2, 12 and 15 as the destination.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660f73da00", "psrldq xmm2, 0"),
    (0x1010, "66410f73dc01", "psrldq xmm12, 1"),
    (0x1020, "660f73da01", "psrldq xmm2, 1"),
    (0x1030, "660f73da08", "psrldq xmm2, 8"),
    (0x1040, "660f73da0f", "psrldq xmm2, 0xf"),
    (0x1050, "660f73da10", "psrldq xmm2, 0x10"),
    (0x1060, "660f73da80", "psrldq xmm2, 0x80"),
    (0x1070, "660f73daff", "psrldq xmm2, 0xff"),
    (0x1080, "66410f73dc05", "psrldq xmm12, 5"),
    (0x1090, "66450f73df0b", "psrldq xmm15, 0xb"),
    (0x10a0, "660f73fa00", "pslldq xmm2, 0"),
    (0x10b0, "66410f73fc01", "pslldq xmm12, 1"),
    (0x10c0, "660f73fa01", "pslldq xmm2, 1"),
    (0x10d0, "660f73fa08", "pslldq xmm2, 8"),
    (0x10e0, "660f73fa0f", "pslldq xmm2, 0xf"),
    (0x10f0, "660f73fa10", "pslldq xmm2, 0x10"),
    (0x1100, "660f73fa80", "pslldq xmm2, 0x80"),
    (0x1110, "660f73faff", "pslldq xmm2, 0xff"),
    (0x1120, "66410f73ff05", "pslldq xmm15, 5"),
    (0x1130, "66410f73fc0b", "pslldq xmm12, 0xb"),
]
