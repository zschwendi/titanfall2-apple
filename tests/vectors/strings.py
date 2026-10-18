"""Hand-written test vectors of the strings family: the string instructions.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x3690, "a4", "movsb byte ptr [rdi], byte ptr [rsi]"),
    (0x36a0, "66a5", "movsw word ptr [rdi], word ptr [rsi]"),
    (0x36b0, "a5", "movsd dword ptr [rdi], dword ptr [rsi]"),
    (0x36c0, "48a5", "movsq qword ptr [rdi], qword ptr [rsi]"),
    (0x36d0, "f3a4", "rep movsb byte ptr [rdi], byte ptr [rsi]"),
    (0x36e0, "f3a5", "rep movsd dword ptr [rdi], dword ptr [rsi]"),
    (0x36f0, "f348a5", "rep movsq qword ptr [rdi], qword ptr [rsi]"),
    (0x3700, "aa", "stosb byte ptr [rdi], al"),
    (0x3710, "ab", "stosd dword ptr [rdi], eax"),
    (0x3720, "f3aa", "rep stosb byte ptr [rdi], al"),
    (0x3730, "f366ab", "rep stosw word ptr [rdi], ax"),
    (0x3740, "f348ab", "rep stosq qword ptr [rdi], rax"),
    (0x3750, "ac", "lodsb al, byte ptr [rsi]"),
    (0x3760, "66ad", "lodsw ax, word ptr [rsi]"),
    (0x3770, "ad", "lodsd eax, dword ptr [rsi]"),
    (0x3780, "48ad", "lodsq rax, qword ptr [rsi]"),
    (0x3790, "ae", "scasb al, byte ptr [rdi]"),
    (0x37a0, "af", "scasd eax, dword ptr [rdi]"),
    (0x37b0, "f2ae", "repne scasb al, byte ptr [rdi]"),
    (0x37c0, "f366af", "repe scasw ax, word ptr [rdi]"),
    (0x37d0, "a6", "cmpsb byte ptr [rsi], byte ptr [rdi]"),
    (0x37e0, "48a7", "cmpsq qword ptr [rsi], qword ptr [rdi]"),
    (0x37f0, "f3a6", "repe cmpsb byte ptr [rsi], byte ptr [rdi]"),
    (0x3800, "f2a7", "repne cmpsd dword ptr [rsi], dword ptr [rdi]"),
    # F2 on a string instruction that is not a compare repeats it like F3; F2/F3 on ret mean nothing
    (0x3e60, "f2a4", "repne movsb byte ptr [rdi], byte ptr [rsi]"),
    (0x3e70, "f2aa", "repne stosb byte ptr [rdi], al"),
    (0x3e80, "f248ab", "repne stosq qword ptr [rdi], rax"),
    (0x3e90, "f248a5", "repne movsq qword ptr [rdi], qword ptr [rsi]"),
    # the two sizes of compare and scan that had no vector
    (0x4400, "66a7", "cmpsw word ptr [rsi], word ptr [rdi]"),
    (0x4410, "f366a7", "repe cmpsw word ptr [rsi], word ptr [rdi]"),
    (0x4420, "48af", "scasq rax, qword ptr [rdi]"),
    (0x4430, "f248af", "repne scasq rax, qword ptr [rdi]"),
]
