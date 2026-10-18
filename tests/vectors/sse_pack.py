"""Hand-written test vectors of the sse_pack family: packuswb, packsswb and packssdw, each in the
xmm,xmm form (with one register twice, and with registers 9 and 12), the xmm,m128 form (with a
register-relative address, a displacement, and a SIB address) and the RIP-relative form, which
reads the 16 bytes at 0x8040.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "660f67ca", "packuswb xmm1, xmm2"),
    (0x1010, "66450f67cc", "packuswb xmm9, xmm12"),
    (0x1020, "660f67db", "packuswb xmm3, xmm3"),
    (0x1030, "660f670b", "packuswb xmm1, xmmword ptr [rbx]"),
    (0x1040, "66440f675310", "packuswb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1050, "660f67548820", "packuswb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1060, "660f670dd86f0000", "packuswb xmm1, xmmword ptr [rip + 0x6fd8]"),
    (0x1070, "660f63ca", "packsswb xmm1, xmm2"),
    (0x1080, "66450f63cc", "packsswb xmm9, xmm12"),
    (0x1090, "660f63db", "packsswb xmm3, xmm3"),
    (0x10a0, "660f630b", "packsswb xmm1, xmmword ptr [rbx]"),
    (0x10b0, "66440f635310", "packsswb xmm10, xmmword ptr [rbx + 0x10]"),
    (0x10c0, "660f63548820", "packsswb xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x10d0, "660f630d686f0000", "packsswb xmm1, xmmword ptr [rip + 0x6f68]"),
    (0x10e0, "660f6bca", "packssdw xmm1, xmm2"),
    (0x10f0, "66450f6bcc", "packssdw xmm9, xmm12"),
    (0x1100, "660f6bdb", "packssdw xmm3, xmm3"),
    (0x1110, "660f6b0b", "packssdw xmm1, xmmword ptr [rbx]"),
    (0x1120, "66440f6b5310", "packssdw xmm10, xmmword ptr [rbx + 0x10]"),
    (0x1130, "660f6b548820", "packssdw xmm2, xmmword ptr [rax + rcx*4 + 0x20]"),
    (0x1140, "660f6b0df86e0000", "packssdw xmm1, xmmword ptr [rip + 0x6ef8]"),
]

# The MMX forms (no 66 prefix), which the game has and the lifter leaves unlifted and reported.
REFUSED = [
    (0x1200, "0f670b", "packuswb mm1, qword ptr [rbx]", "opcode f67 /1 is not a form the lifter translates"),
    (0x1210, "0f630b", "packsswb mm1, qword ptr [rbx]", "opcode f63 /1 is not a form the lifter translates"),
    (0x1220, "0f6b0b", "packssdw mm1, qword ptr [rbx]", "opcode f6b /1 is not a form the lifter translates"),
]
