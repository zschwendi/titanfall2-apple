"""Hand-written test vectors of the sse_movmsk family: movmskps, movmskpd and pmovmskb, each
with the register pair ecx, xmm2 and the high pair r9d, xmm12 (the only shape the game has is
r32, xmm).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "0f50ca", "movmskps ecx, xmm2"),                    # movmskps | r32,x | enc(0x0F50, mand=0)
    (0x1010, "450f50cc", "movmskps r9d, xmm12"),                 # movmskps | r32,x | enc(0x0F50, mand=0)
    (0x1020, "660f50ca", "movmskpd ecx, xmm2"),                  # movmskpd | r32,x | enc(0x0F50, mand=0x66)
    (0x1030, "66450f50cc", "movmskpd r9d, xmm12"),               # movmskpd | r32,x | enc(0x0F50, mand=0x66)
    (0x1040, "660fd7ca", "pmovmskb ecx, xmm2"),                  # pmovmskb | r32,x | enc(0x0FD7, mand=0x66)
    (0x1050, "66450fd7cc", "pmovmskb r9d, xmm12"),               # pmovmskb | r32,x | enc(0x0FD7, mand=0x66)
]
