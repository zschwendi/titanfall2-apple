"""Hand-written test vectors of the control family: jcc, jmp, call, ret.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x2c30, "0f80cae2ffff", "jo 0xf00"),
    (0x2c90, "0f816ae2ffff", "jno 0xf00"),
    (0x2cf0, "0f820ae2ffff", "jb 0xf00"),
    (0x2d50, "0f83aae1ffff", "jae 0xf00"),
    (0x2db0, "0f844ae1ffff", "je 0xf00"),
    (0x2e10, "0f85eae0ffff", "jne 0xf00"),
    (0x2e70, "0f868ae0ffff", "jbe 0xf00"),
    (0x2ed0, "0f872ae0ffff", "ja 0xf00"),
    (0x2f30, "0f88cadfffff", "js 0xf00"),
    (0x2f90, "0f896adfffff", "jns 0xf00"),
    (0x2ff0, "0f8a0adfffff", "jp 0xf00"),
    (0x3050, "0f8baadeffff", "jnp 0xf00"),
    (0x30b0, "0f8c4adeffff", "jl 0xf00"),
    (0x3110, "0f8deaddffff", "jge 0xf00"),
    (0x3170, "0f8e8addffff", "jle 0xf00"),
    (0x31d0, "0f8f2addffff", "jg 0xf00"),
    (0x31e0, "e31e", "jrcxz 0x3200"),
    (0x31f0, "67e30d", "jecxz 0x3200"),
    (0x3210, "e9ebdcffff", "jmp 0xf00"),
    (0x3220, "ffe0", "jmp rax"),
    (0x3230, "ff23", "jmp qword ptr [rbx]"),
    (0x3240, "ff25ba4d0000", "jmp qword ptr [rip + 0x4dba]"),
    (0x3250, "e8abdcffff", "call 0xf00"),
    (0x3260, "ffd0", "call rax"),
    (0x3270, "ff5308", "call qword ptr [rbx + 8]"),
    (0x3280, "ff157a4d0000", "call qword ptr [rip + 0x4d7a]"),
    (0x3290, "41ffd3", "call r11"),
    (0x32a0, "c3", "ret"),
    (0x32b0, "c21800", "ret 0x18"),
    (0x3ea0, "f3c3", "repz ret"),
    (0x3eb0, "f2c3", "bnd ret"),
]
