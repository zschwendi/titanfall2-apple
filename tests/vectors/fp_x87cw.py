"""Hand-written test vectors of the fp_x87cw family: fnstcw and fldcw with a 16-bit memory operand
(on the stack, through a register, through r12/r13, RIP-relative) and fnclex; and x87
instructions that must stay refused (fninit, a load, an environment store, an exchange).

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "d93c24", "fnstcw word ptr [rsp]"),
    (0x1010, "d97c2408", "fnstcw word ptr [rsp + 8]"),
    (0x1020, "d97b10", "fnstcw word ptr [rbx + 0x10]"),
    (0x1030, "41d97d00", "fnstcw word ptr [r13]"),
    (0x1040, "d93d00700000", "fnstcw word ptr [rip + 0x7000]"),
    (0x1050, "d92c24", "fldcw word ptr [rsp]"),
    (0x1060, "d96c2408", "fldcw word ptr [rsp + 8]"),
    (0x1070, "d92b", "fldcw word ptr [rbx]"),
    (0x1080, "41d96c2420", "fldcw word ptr [r12 + 0x20]"),
    (0x1090, "d92d00700000", "fldcw word ptr [rip + 0x7000]"),
    (0x10a0, "dbe2", "fnclex"),
]

REFUSED = [
    (0x2000, "dbe3", "fninit", "fninit"),
    (0x2010, "d9e8", "fld1", "fld1"),
    (0x2020, "d93424", "fnstenv [rsp]", "fnstenv"),
    (0x2030, "d9c9", "fxch st(1)", "fxch"),
]
