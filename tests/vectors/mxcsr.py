"""Hand-written test vectors of the mxcsr family: stmxcsr, ldmxcsr.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "0fae1b", "stmxcsr dword ptr [rbx]"),
    (0x1010, "0fae5c2410", "stmxcsr dword ptr [rsp + 0x10]"),
    (0x1020, "0fae13", "ldmxcsr dword ptr [rbx]"),
    (0x1030, "0fae542410", "ldmxcsr dword ptr [rsp + 0x10]"),
]
