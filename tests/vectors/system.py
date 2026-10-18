"""Hand-written test vectors of the system family: traps, cpuid, rdtsc, flag operations, nop and friends.

(rva, hex bytes, assembly as the decoder prints it).  Layout and rules: tests/vectors/__init__.py.
"""

VECTORS = [
    (0x3200, "90", "nop"),
    (0x3810, "f8", "clc"),
    (0x3820, "f9", "stc"),
    (0x3830, "f5", "cmc"),
    (0x3840, "fc", "cld"),
    (0x3850, "fd", "std"),
    (0x3860, "90", "nop"),
    (0x3870, "0f1f0400", "nop dword ptr [rax + rax]"),
    (0x3880, "f390", "pause"),
    (0x3890, "0faef0", "mfence"),
    (0x38a0, "0faee8", "lfence"),
    (0x38b0, "0faef8", "sfence"),
    (0x38c0, "0f1803", "prefetchnta byte ptr [rbx]"),
    (0x38d0, "0fa2", "cpuid"),
    (0x38e0, "0f31", "rdtsc"),
    # instructions that always trap (hand-encoded; added with review finding 4)
    (0x3d50, "cc", "int3"),
    (0x3d60, "0f0b", "ud2"),
    (0x3d70, "f4", "hlt"),
    (0x3d80, "cd29", "int 0x29"),
    # the other prefetch hints: all of them do nothing to guest state
    (0x4400, "0f180b", "prefetcht0 byte ptr [rbx]"),
    (0x4410, "0f1813", "prefetcht1 byte ptr [rbx]"),
    (0x4420, "0f181b", "prefetcht2 byte ptr [rbx]"),
    (0x4430, "0f0d0b", "prefetchw byte ptr [rbx]"),
    (0x4440, "0f0d03", "prefetch byte ptr [rbx]"),
]
