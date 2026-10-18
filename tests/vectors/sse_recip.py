"""Hand-written test vectors of the sse_recip family: rcpss, rsqrtss, rcpps, rsqrtps.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

The expected side of every case is the real CPU: the emulator computes the exact quotient, which
is not what these instructions return (harness/oraclefix.py, HARDWARE_ONLY).  The first vector of
each mnemonic, `<op> xmm1, xmm2`, is also the instruction harness/recip_sweep.py sweeps.

A 16-byte memory operand must be 16-byte aligned (#GP otherwise).  The harness places a
register-relative operand itself: aligned in four states of five, misaligned in the fifth.  A
RIP-relative operand lies where the encoding puts it, so both kinds are written out here.
"""

VECTORS = [
    (0x1000, "f30f53ca", "rcpss xmm1, xmm2"),
    (0x1010, "f3450f53cc", "rcpss xmm9, xmm12"),
    (0x1020, "f30f53db", "rcpss xmm3, xmm3"),
    (0x1030, "f30f530b", "rcpss xmm1, dword ptr [rbx]"),
    (0x1040, "f3440f535310", "rcpss xmm10, dword ptr [rbx + 0x10]"),
    (0x1050, "f30f53548820", "rcpss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f30f530dd86f0000", "rcpss xmm1, dword ptr [rip + 0x6fd8]"),
    (0x1070, "f30f52ca", "rsqrtss xmm1, xmm2"),
    (0x1080, "f3450f52cc", "rsqrtss xmm9, xmm12"),
    (0x1090, "f30f52db", "rsqrtss xmm3, xmm3"),
    (0x10a0, "f30f520b", "rsqrtss xmm1, dword ptr [rbx]"),
    (0x10b0, "f3440f525310", "rsqrtss xmm10, dword ptr [rbx + 0x10]"),
    (0x10c0, "f30f52548820", "rsqrtss xmm2, dword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "f30f520d686f0000", "rsqrtss xmm1, dword ptr [rip + 0x6f68]"),
    (0x10e0, "0f53ca", "rcpps xmm1, xmm2"),
    (0x10f0, "450f53cc", "rcpps xmm9, xmm12"),
    (0x1100, "0f53db", "rcpps xmm3, xmm3"),
    (0x1110, "0f530b", "rcpps xmm1, xmmword ptr [rbx]"),
    (0x1120, "440f535310", "rcpps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1130, "0f53548820", "rcpps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1140, "0f530df96e0000", "rcpps xmm1, xmmword ptr [rip + 0x6ef9]"),      # the operand is at 0x8040: aligned
    (0x1150, "0f52ca", "rsqrtps xmm1, xmm2"),
    (0x1160, "450f52cc", "rsqrtps xmm9, xmm12"),
    (0x1170, "0f52db", "rsqrtps xmm3, xmm3"),
    (0x1180, "0f520b", "rsqrtps xmm1, xmmword ptr [rbx]"),
    (0x1190, "440f525310", "rsqrtps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x11a0, "0f52548820", "rsqrtps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x11b0, "0f520d896e0000", "rsqrtps xmm1, xmmword ptr [rip + 0x6e89]"),      # the operand is at 0x8040: aligned
    (0x11c0, "0f535c2420", "rcpps xmm3, xmmword ptr [rsp + 0x20]"),
    (0x11d0, "0f525c2420", "rsqrtps xmm3, xmmword ptr [rsp + 0x20]"),
    (0x11e0, "f30f535c2420", "rcpss xmm3, dword ptr [rsp + 0x20]"),
    (0x11f0, "f30f525c2420", "rsqrtss xmm3, dword ptr [rsp + 0x20]"),
    (0x1200, "0f530d416e0000", "rcpps xmm1, xmmword ptr [rip + 0x6e41]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x1210, "0f520d0a6e0000", "rsqrtps xmm1, xmmword ptr [rip + 0x6e0a]"),      # the operand is at 0x8021: not aligned, #GP in every state
]
