"""Hand-written test vectors of the sse_psra family: psraw and psrad, arithmetic right shifts of the
signed lanes, with the encodings 66 0F E1 /r, 66 0F 71 /4 ib, 66 0F E2 /r and 66 0F 72 /4 ib.

Every operand form the game has: the xmm, imm8 form with the counts 0, 1, 5, 8, 0xf, 0x10, 0x1f,
0x20, 0x80 and 0xff; the xmm, xmm form with two low registers (xmm1, xmm2), with two high ones (xmm9, xmm12) and with
one register against itself (xmm3); the xmm, m128 form with a base, a base and displacement,
an index, and RIP-relative addressing (its data at 0x8040, which is aligned). The MMX forms of the
same opcodes (no 66 prefix) are refused.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660fe1ca", "psraw xmm1, xmm2"),                    # psraw | x,x | enc(0x0FE1, mand=0x66) | family sse_psra
    (0x1010, "66450fe1cc", "psraw xmm9, xmm12"),                 # psraw | x,x | enc(0x0FE1, mand=0x66) | family sse_psra
    (0x1020, "660fe1db", "psraw xmm3, xmm3"),                    # psraw | x,x | enc(0x0FE1, mand=0x66) | family sse_psra
    (0x1030, "660fe10b", "psraw xmm1, xmmword ptr [rbx]"),       # psraw | x,m128 | enc(0x0FE1, mand=0x66) | family sse_psra
    (0x1040, "66440fe15310", "psraw xmm10, xmmword ptr [rbx + 0x10]"), # psraw | x,m128 | enc(0x0FE1, mand=0x66) | family sse_psra
    (0x1050, "660fe1548820", "psraw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # psraw | x,m128 | enc(0x0FE1, mand=0x66) | family sse_psra
    (0x1060, "660fe10dd86f0000", "psraw xmm1, xmmword ptr [rip + 0x6fd8]"), # psraw | x,m128rip | enc(0x0FE1, mand=0x66) | family sse_psra
    (0x1100, "660f71e200", "psraw xmm2, 0"),                     # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1110, "660f71e201", "psraw xmm2, 1"),                     # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1120, "660f71e205", "psraw xmm2, 5"),                     # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1130, "660f71e208", "psraw xmm2, 8"),                     # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1140, "660f71e20f", "psraw xmm2, 0xf"),                   # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1150, "660f71e210", "psraw xmm2, 0x10"),                  # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1160, "660f71e21f", "psraw xmm2, 0x1f"),                  # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1170, "660f71e220", "psraw xmm2, 0x20"),                  # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1180, "660f71e280", "psraw xmm2, 0x80"),                  # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1190, "660f71e2ff", "psraw xmm2, 0xff"),                  # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x11a0, "66410f71e400", "psraw xmm12, 0"),                  # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x11b0, "66410f71e401", "psraw xmm12, 1"),                  # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x11c0, "66410f71e405", "psraw xmm12, 5"),                  # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x11d0, "66410f71e408", "psraw xmm12, 8"),                  # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x11e0, "66410f71e40f", "psraw xmm12, 0xf"),                # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x11f0, "66410f71e410", "psraw xmm12, 0x10"),               # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1200, "66410f71e41f", "psraw xmm12, 0x1f"),               # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1210, "66410f71e420", "psraw xmm12, 0x20"),               # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1220, "66410f71e480", "psraw xmm12, 0x80"),               # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1230, "66410f71e4ff", "psraw xmm12, 0xff"),               # psraw | x,i | enc(0x0F71, reg=4, mand=0x66) | family sse_psra
    (0x1400, "660fe2ca", "psrad xmm1, xmm2"),                    # psrad | x,x | enc(0x0FE2, mand=0x66) | family sse_psra
    (0x1410, "66450fe2cc", "psrad xmm9, xmm12"),                 # psrad | x,x | enc(0x0FE2, mand=0x66) | family sse_psra
    (0x1420, "660fe2db", "psrad xmm3, xmm3"),                    # psrad | x,x | enc(0x0FE2, mand=0x66) | family sse_psra
    (0x1430, "660fe20b", "psrad xmm1, xmmword ptr [rbx]"),       # psrad | x,m128 | enc(0x0FE2, mand=0x66) | family sse_psra
    (0x1440, "66440fe25310", "psrad xmm10, xmmword ptr [rbx + 0x10]"), # psrad | x,m128 | enc(0x0FE2, mand=0x66) | family sse_psra
    (0x1450, "660fe2548820", "psrad xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # psrad | x,m128 | enc(0x0FE2, mand=0x66) | family sse_psra
    (0x1460, "660fe20dd86b0000", "psrad xmm1, xmmword ptr [rip + 0x6bd8]"), # psrad | x,m128rip | enc(0x0FE2, mand=0x66) | family sse_psra
    (0x1500, "660f72e200", "psrad xmm2, 0"),                     # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1510, "660f72e201", "psrad xmm2, 1"),                     # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1520, "660f72e205", "psrad xmm2, 5"),                     # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1530, "660f72e208", "psrad xmm2, 8"),                     # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1540, "660f72e20f", "psrad xmm2, 0xf"),                   # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1550, "660f72e210", "psrad xmm2, 0x10"),                  # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1560, "660f72e21f", "psrad xmm2, 0x1f"),                  # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1570, "660f72e220", "psrad xmm2, 0x20"),                  # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1580, "660f72e280", "psrad xmm2, 0x80"),                  # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1590, "660f72e2ff", "psrad xmm2, 0xff"),                  # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x15a0, "66410f72e400", "psrad xmm12, 0"),                  # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x15b0, "66410f72e401", "psrad xmm12, 1"),                  # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x15c0, "66410f72e405", "psrad xmm12, 5"),                  # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x15d0, "66410f72e408", "psrad xmm12, 8"),                  # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x15e0, "66410f72e40f", "psrad xmm12, 0xf"),                # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x15f0, "66410f72e410", "psrad xmm12, 0x10"),               # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1600, "66410f72e41f", "psrad xmm12, 0x1f"),               # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1610, "66410f72e420", "psrad xmm12, 0x20"),               # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1620, "66410f72e480", "psrad xmm12, 0x80"),               # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
    (0x1630, "66410f72e4ff", "psrad xmm12, 0xff"),               # psrad | x,i | enc(0x0F72, reg=4, mand=0x66) | family sse_psra
]

REFUSED = [
    (0x1700, "0fe10b", "psraw mm1, qword ptr [rbx]", "opcode fe1 /1 is not a form the lifter translates"),            # psraw | reg?,m64 | enc(0x0FE1, mand=0) | family sse_psra
    (0x1710, "0fe20b", "psrad mm1, qword ptr [rbx]", "opcode fe2 /1 is not a form the lifter translates"),            # psrad | reg?,m64 | enc(0x0FE2, mand=0) | family sse_psra
]
