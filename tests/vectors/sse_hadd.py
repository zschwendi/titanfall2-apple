"""Hand-written test vectors of the sse_hadd family: addsubps, haddps.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.

A 16-byte memory operand must be 16-byte aligned (#GP otherwise).  The harness places a
register-relative operand itself: aligned in four states of five, misaligned in the fifth.  A
RIP-relative operand lies where the encoding puts it, so both kinds are written out here.
"""

VECTORS = [
    (0x1000, "f20fd0ca", "addsubps xmm1, xmm2"),
    (0x1010, "f2450fd0cc", "addsubps xmm9, xmm12"),
    (0x1020, "f20fd0db", "addsubps xmm3, xmm3"),
    (0x1030, "f20fd00b", "addsubps xmm1, xmmword ptr [rbx]"),
    (0x1040, "f2440fd05310", "addsubps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "f20fd0548820", "addsubps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "f20fd00dd86f0000", "addsubps xmm1, xmmword ptr [rip + 0x6fd8]"),      # the operand is at 0x8040: aligned
    (0x1070, "f20fd00dd06f0000", "addsubps xmm1, xmmword ptr [rip + 0x6fd0]"),      # the operand is at 0x8048: not aligned, #GP in every state
    (0x1080, "f20fd05c2420", "addsubps xmm3, xmmword ptr [rsp + 0x20]"),
    (0x1100, "f20f7cca", "haddps xmm1, xmm2"),
    (0x1110, "f2450f7ccc", "haddps xmm9, xmm12"),
    (0x1120, "f20f7cdb", "haddps xmm3, xmm3"),
    (0x1130, "f20f7c0b", "haddps xmm1, xmmword ptr [rbx]"),
    (0x1140, "f2440f7c5310", "haddps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "f20f7c548820", "haddps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "f20f7c0dd86e0000", "haddps xmm1, xmmword ptr [rip + 0x6ed8]"),      # the operand is at 0x8040: aligned
    (0x1170, "f20f7c0da96e0000", "haddps xmm1, xmmword ptr [rip + 0x6ea9]"),      # the operand is at 0x8021: not aligned, #GP in every state
    (0x1180, "f20f7c5c2420", "haddps xmm3, xmmword ptr [rsp + 0x20]"),
]
