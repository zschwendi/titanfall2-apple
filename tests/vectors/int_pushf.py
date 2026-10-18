"""Hand-written test vectors of the int_pushf family: pushfq and popfq, plain and with a REX.W
prefix (which changes nothing); the 16-bit forms pushf and popf must be refused.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x1000, "9c", "pushfq"),
    (0x1010, "489c", "pushfq"),
    (0x1020, "9d", "popfq"),
    (0x1030, "489d", "popfq"),
]

REFUSED = [
    (0x1040, "669c", "pushf", "pushf"),
    (0x1050, "669d", "popf", "popf"),
]
