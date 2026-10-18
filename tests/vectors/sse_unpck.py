"""Hand-written test vectors of the sse_unpck family: unpcklps, unpckhps, unpcklpd and unpckhpd,
the interleaves of the low or high halves of two xmm registers, with the forms x,x (including a
register with itself and the high registers 8 to 15), x,m128 (with a high destination, a
displacement, and an index) and x,m128rip (data at 0x8040).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "0f14ca", "unpcklps xmm1, xmm2"),
    (0x1010, "450f14cc", "unpcklps xmm9, xmm12"),
    (0x1020, "0f14db", "unpcklps xmm3, xmm3"),
    (0x1030, "0f140b", "unpcklps xmm1, xmmword ptr [rbx]"),
    (0x1040, "440f145310", "unpcklps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "0f14548820", "unpcklps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "0f140dd96f0000", "unpcklps xmm1, xmmword ptr [rip + 0x6fd9]"),
    (0x1100, "0f15ca", "unpckhps xmm1, xmm2"),
    (0x1110, "450f15cc", "unpckhps xmm9, xmm12"),
    (0x1120, "0f15db", "unpckhps xmm3, xmm3"),
    (0x1130, "0f150b", "unpckhps xmm1, xmmword ptr [rbx]"),
    (0x1140, "440f155310", "unpckhps xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1150, "0f15548820", "unpckhps xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1160, "0f150dd96e0000", "unpckhps xmm1, xmmword ptr [rip + 0x6ed9]"),
    (0x1200, "660f14ca", "unpcklpd xmm1, xmm2"),
    (0x1210, "66450f14cc", "unpcklpd xmm9, xmm12"),
    (0x1220, "660f14db", "unpcklpd xmm3, xmm3"),
    (0x1230, "660f140b", "unpcklpd xmm1, xmmword ptr [rbx]"),
    (0x1240, "66440f145310", "unpcklpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1250, "660f14548820", "unpcklpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1260, "660f140dd86d0000", "unpcklpd xmm1, xmmword ptr [rip + 0x6dd8]"),
    (0x1300, "660f15ca", "unpckhpd xmm1, xmm2"),
    (0x1310, "66450f15cc", "unpckhpd xmm9, xmm12"),
    (0x1320, "660f15db", "unpckhpd xmm3, xmm3"),
    (0x1330, "660f150b", "unpckhpd xmm1, xmmword ptr [rbx]"),
    (0x1340, "66440f155310", "unpckhpd xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1350, "660f15548820", "unpckhpd xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1360, "660f150dd86c0000", "unpckhpd xmm1, xmmword ptr [rip + 0x6cd8]"),
]
