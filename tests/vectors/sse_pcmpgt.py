"""Hand-written test vectors of the sse_pcmpgt family: pcmpgtb, pcmpgtw, pcmpgtd, packed signed
compares, each in the xmm,xmm form (with a register with itself, and with a register numbered 8
or above), the xmm,m128 form and the xmm,m128 RIP-relative form.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660f64ca", "pcmpgtb xmm1, xmm2"),
    (0x1010, "66450f64cc", "pcmpgtb xmm9, xmm12"),
    (0x1020, "660f64db", "pcmpgtb xmm3, xmm3"),
    (0x1030, "660f640b", "pcmpgtb xmm1, xmmword ptr [rbx]"),
    (0x1040, "660f640df86f0000", "pcmpgtb xmm1, xmmword ptr [rip + 0x6ff8]"),
    (0x1100, "660f65ca", "pcmpgtw xmm1, xmm2"),
    (0x1110, "66450f65cc", "pcmpgtw xmm9, xmm12"),
    (0x1120, "660f65db", "pcmpgtw xmm3, xmm3"),
    (0x1130, "660f650b", "pcmpgtw xmm1, xmmword ptr [rbx]"),
    (0x1140, "660f650df86e0000", "pcmpgtw xmm1, xmmword ptr [rip + 0x6ef8]"),
    (0x1200, "660f66ca", "pcmpgtd xmm1, xmm2"),
    (0x1210, "66450f66cc", "pcmpgtd xmm9, xmm12"),
    (0x1220, "660f66db", "pcmpgtd xmm3, xmm3"),
    (0x1230, "660f660b", "pcmpgtd xmm1, xmmword ptr [rbx]"),
    (0x1240, "660f660df86d0000", "pcmpgtd xmm1, xmmword ptr [rip + 0x6df8]"),
]
