"""Test vectors for movnti: every operand form the game has (m32,r32 and m64,r64; base, index and RIP-relative; r10 as source)."""

VECTORS = [
    (0x1000, "0fc30b", "movnti dword ptr [rbx], ecx"),
    (0x1010, "440fc35310", "movnti dword ptr [rbx + 0x10], r10d"),
    (0x1020, "0fc3548820", "movnti dword ptr [rax + rcx*4 + 0x20], edx"),
    (0x1030, "0fc30d09700000", "movnti dword ptr [rip + 0x7009], ecx"),
    (0x1040, "480fc30b", "movnti qword ptr [rbx], rcx"),
    (0x1050, "4c0fc35310", "movnti qword ptr [rbx + 0x10], r10"),
    (0x1060, "480fc3548820", "movnti qword ptr [rax + rcx*4 + 0x20], rdx"),
    (0x1070, "480fc30dc86f0000", "movnti qword ptr [rip + 0x6fc8], rcx"),
]
