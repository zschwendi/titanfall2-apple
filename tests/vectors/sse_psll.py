"""Hand-written test vectors of the sse_psll family: psllw, pslld and psllq, each with the
immediate count (66 0F 71/72/73 /6 ib, counts 0, 1, 5, 8, 0xf, 0x10, 0x1f, 0x20, 0x3f, 0x40, 0x80
and 0xff, in an xmm2 and an xmm12 destination) and the xmm or 16-byte memory count (66 0F F1/F2/F3
/r: register with register, the same register twice, and memory with a base register, a
displacement, an index and RIP-relative).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660ff1ca", "psllw xmm1, xmm2"),                    # psllw | x,x | enc(0x0FF1, mand=0x66) | family sse_psll
    (0x1010, "66450ff1cc", "psllw xmm9, xmm12"),                 # psllw | x,x | enc(0x0FF1, mand=0x66) | family sse_psll
    (0x1020, "660ff1db", "psllw xmm3, xmm3"),                    # psllw | x,x | enc(0x0FF1, mand=0x66) | family sse_psll
    (0x1030, "660ff10b", "psllw xmm1, xmmword ptr [rbx]"),       # psllw | x,m128 | enc(0x0FF1, mand=0x66) | family sse_psll
    (0x1040, "66440ff15310", "psllw xmm10, xmmword ptr [rbx + 0x10]"), # psllw | x,m128 | enc(0x0FF1, mand=0x66) | family sse_psll
    (0x1050, "660ff1548820", "psllw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # psllw | x,m128 | enc(0x0FF1, mand=0x66) | family sse_psll
    (0x1060, "660ff10dd86f0000", "psllw xmm1, xmmword ptr [rip + 0x6fd8]"), # psllw | x,m128rip | enc(0x0FF1, mand=0x66) | family sse_psll
    (0x1070, "660f71f200", "psllw xmm2, 0"),                     # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1080, "660f71f201", "psllw xmm2, 1"),                     # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1090, "660f71f205", "psllw xmm2, 5"),                     # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x10a0, "660f71f208", "psllw xmm2, 8"),                     # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x10b0, "660f71f20f", "psllw xmm2, 0xf"),                   # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x10c0, "660f71f210", "psllw xmm2, 0x10"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x10d0, "660f71f21f", "psllw xmm2, 0x1f"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x10e0, "660f71f220", "psllw xmm2, 0x20"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x10f0, "660f71f23f", "psllw xmm2, 0x3f"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1100, "660f71f240", "psllw xmm2, 0x40"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1110, "660f71f280", "psllw xmm2, 0x80"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1120, "660f71f2ff", "psllw xmm2, 0xff"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1130, "66410f71f400", "psllw xmm12, 0"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1140, "66410f71f401", "psllw xmm12, 1"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1150, "66410f71f405", "psllw xmm12, 5"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1160, "66410f71f408", "psllw xmm12, 8"),                  # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1170, "66410f71f40f", "psllw xmm12, 0xf"),                # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1180, "66410f71f410", "psllw xmm12, 0x10"),               # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x1190, "66410f71f41f", "psllw xmm12, 0x1f"),               # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x11a0, "66410f71f420", "psllw xmm12, 0x20"),               # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x11b0, "66410f71f43f", "psllw xmm12, 0x3f"),               # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x11c0, "66410f71f440", "psllw xmm12, 0x40"),               # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x11d0, "66410f71f480", "psllw xmm12, 0x80"),               # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x11e0, "66410f71f4ff", "psllw xmm12, 0xff"),               # psllw | x,i | enc(0x0F71, reg=6, mand=0x66) | family sse_psll
    (0x11f0, "660ff2ca", "pslld xmm1, xmm2"),                    # pslld | x,x | enc(0x0FF2, mand=0x66) | family sse_psll
    (0x1200, "66450ff2cc", "pslld xmm9, xmm12"),                 # pslld | x,x | enc(0x0FF2, mand=0x66) | family sse_psll
    (0x1210, "660ff2db", "pslld xmm3, xmm3"),                    # pslld | x,x | enc(0x0FF2, mand=0x66) | family sse_psll
    (0x1220, "660ff20b", "pslld xmm1, xmmword ptr [rbx]"),       # pslld | x,m128 | enc(0x0FF2, mand=0x66) | family sse_psll
    (0x1230, "66440ff25310", "pslld xmm10, xmmword ptr [rbx + 0x10]"), # pslld | x,m128 | enc(0x0FF2, mand=0x66) | family sse_psll
    (0x1240, "660ff2548820", "pslld xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # pslld | x,m128 | enc(0x0FF2, mand=0x66) | family sse_psll
    (0x1250, "660ff20de86d0000", "pslld xmm1, xmmword ptr [rip + 0x6de8]"), # pslld | x,m128rip | enc(0x0FF2, mand=0x66) | family sse_psll
    (0x12c0, "660f72f200", "pslld xmm2, 0"),                     # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x12d0, "660f72f201", "pslld xmm2, 1"),                     # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x12e0, "660f72f205", "pslld xmm2, 5"),                     # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x12f0, "660f72f208", "pslld xmm2, 8"),                     # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1300, "660f72f20f", "pslld xmm2, 0xf"),                   # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1310, "660f72f210", "pslld xmm2, 0x10"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1320, "660f72f21f", "pslld xmm2, 0x1f"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1330, "660f72f220", "pslld xmm2, 0x20"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1340, "660f72f23f", "pslld xmm2, 0x3f"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1350, "660f72f240", "pslld xmm2, 0x40"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1360, "660f72f280", "pslld xmm2, 0x80"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1370, "660f72f2ff", "pslld xmm2, 0xff"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1380, "66410f72f400", "pslld xmm12, 0"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1390, "66410f72f401", "pslld xmm12, 1"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x13a0, "66410f72f405", "pslld xmm12, 5"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x13b0, "66410f72f408", "pslld xmm12, 8"),                  # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x13c0, "66410f72f40f", "pslld xmm12, 0xf"),                # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x13d0, "66410f72f410", "pslld xmm12, 0x10"),               # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x13e0, "66410f72f41f", "pslld xmm12, 0x1f"),               # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x13f0, "66410f72f420", "pslld xmm12, 0x20"),               # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1400, "66410f72f43f", "pslld xmm12, 0x3f"),               # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1410, "66410f72f440", "pslld xmm12, 0x40"),               # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1420, "66410f72f480", "pslld xmm12, 0x80"),               # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1430, "66410f72f4ff", "pslld xmm12, 0xff"),               # pslld | x,i | enc(0x0F72, reg=6, mand=0x66) | family sse_psll
    (0x1440, "660ff3ca", "psllq xmm1, xmm2"),                    # psllq | x,x | enc(0x0FF3, mand=0x66) | family sse_psll
    (0x1450, "66450ff3cc", "psllq xmm9, xmm12"),                 # psllq | x,x | enc(0x0FF3, mand=0x66) | family sse_psll
    (0x1460, "660ff3db", "psllq xmm3, xmm3"),                    # psllq | x,x | enc(0x0FF3, mand=0x66) | family sse_psll
    (0x1470, "660ff30b", "psllq xmm1, xmmword ptr [rbx]"),       # psllq | x,m128 | enc(0x0FF3, mand=0x66) | family sse_psll
    (0x1480, "66440ff35310", "psllq xmm10, xmmword ptr [rbx + 0x10]"), # psllq | x,m128 | enc(0x0FF3, mand=0x66) | family sse_psll
    (0x1490, "660ff3548820", "psllq xmm2, xmmword ptr [rax + rcx*4 + 0x20]"), # psllq | x,m128 | enc(0x0FF3, mand=0x66) | family sse_psll
    (0x14a0, "660ff30d986b0000", "psllq xmm1, xmmword ptr [rip + 0x6b98]"), # psllq | x,m128rip | enc(0x0FF3, mand=0x66) | family sse_psll
    (0x1510, "660f73f200", "psllq xmm2, 0"),                     # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1520, "660f73f201", "psllq xmm2, 1"),                     # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1530, "660f73f205", "psllq xmm2, 5"),                     # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1540, "660f73f208", "psllq xmm2, 8"),                     # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1550, "660f73f20f", "psllq xmm2, 0xf"),                   # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1560, "660f73f210", "psllq xmm2, 0x10"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1570, "660f73f21f", "psllq xmm2, 0x1f"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1580, "660f73f220", "psllq xmm2, 0x20"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1590, "660f73f23f", "psllq xmm2, 0x3f"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x15a0, "660f73f240", "psllq xmm2, 0x40"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x15b0, "660f73f280", "psllq xmm2, 0x80"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x15c0, "660f73f2ff", "psllq xmm2, 0xff"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x15d0, "66410f73f400", "psllq xmm12, 0"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x15e0, "66410f73f401", "psllq xmm12, 1"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x15f0, "66410f73f405", "psllq xmm12, 5"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1600, "66410f73f408", "psllq xmm12, 8"),                  # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1610, "66410f73f40f", "psllq xmm12, 0xf"),                # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1620, "66410f73f410", "psllq xmm12, 0x10"),               # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1630, "66410f73f41f", "psllq xmm12, 0x1f"),               # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1640, "66410f73f420", "psllq xmm12, 0x20"),               # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1650, "66410f73f43f", "psllq xmm12, 0x3f"),               # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1660, "66410f73f440", "psllq xmm12, 0x40"),               # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1670, "66410f73f480", "psllq xmm12, 0x80"),               # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
    (0x1680, "66410f73f4ff", "psllq xmm12, 0xff"),               # psllq | x,i | enc(0x0F73, reg=6, mand=0x66) | family sse_psll
]

# The MMX forms (no 66 prefix), which the game has and the lifter leaves unlifted and reported.
REFUSED = [
    (0x1700, "0ff10b", "psllw mm1, qword ptr [rbx]", "opcode ff1 /1 is not a form the lifter translates"),
    (0x1710, "0ff20b", "pslld mm1, qword ptr [rbx]", "opcode ff2 /1 is not a form the lifter translates"),
    (0x1720, "0ff30b", "psllq mm1, qword ptr [rbx]", "opcode ff3 /1 is not a form the lifter translates"),
]
