"""Hand-written test vectors of the int_loop family: loop, loopne.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "e20e", "loop 0x1010"),
    (0x1010, "e2ee", "loop 0x1000"),
    (0x1020, "e01e", "loopne 0x1040"),
    (0x1030, "e0ee", "loopne 0x1020"),
]

REFUSED = [
    (0x1040, "67e20d", "loop 0x1050", "loop with a 32-bit counter"),
]
