"""Self-test data for the stops (lifter/stops.py): an instruction the lifter decodes and does
not translate, inside a function it lifts all the same.  No game files are involved.  The
underscore keeps this file out of the instruction families (tests/vectors/__init__.py).

STOP_PROGRAMS    whole functions, an image of their own (run_selftests.stop_image)
STOP_VECTORS     single instructions that must become a stop, with the features they wait for
STOP_REFUSED     single instructions that must still make the lifter skip the function

Where the expectations come from.  That an instruction is a stop or not follows from the rule
in lifter/stops.py (it falls through, and it is not a transfer of control); the features come
from the Intel SDM, vol. 2: the "CPUID Feature Flag" column of each instruction's page and the
#UD conditions listed there (XGETBV, MOVBE and ADCX were read again on 2026-10-10 in the copy
at felixcloutier.com/x86; the others are from memory).  None comes from the lifter's output.
"""

# The functions.  cap_fn has the shape of the routine that made this necessary (finding 38,
# engine+0x55c430: fill the capability words, and read XCR0 only if CPUID says the OS enabled
# it; the original tests the bit with bt, here it is a test); guard_fn the shape of the three that test a capability bit inside themselves and have a
# plain path beside the extension's.  Both are constructed here, not copied.
STOP_PROGRAMS = {
    "cap_fn": (0x1000, [
        ("53", "push rbx"),
        ("b801000000", "mov eax, 1"),
        ("0fa2", "cpuid"),
        ("f7c100000008", "test ecx, 0x8000000"),          # bit 27: OSXSAVE
        ("7408", "je 0x1018"),
        ("31c9", "xor ecx, ecx"),
        ("0f01d0", "xgetbv"),                             # 0x1012: the stop
        ("83e006", "and eax, 6"),
        ("89d0", "mov eax, edx"),
        ("5b", "pop rbx"),
        ("c3", "ret"),
    ]),
    "guard_fn": (0x1040, [
        ("f6c101", "test cl, 1"),
        ("7504", "jne 0x1049"),
        ("8d4211", "lea eax, [rdx + 0x11]"),              # the plain path
        ("c3", "ret"),
        ("c5fd6fc1", "vmovdqa ymm0, ymm1"),               # 0x1049: the extension's path, two stops
        ("c5f877", "vzeroupper"),
        ("c3", "ret"),
    ]),
    "mmx_fn": (0x1080, [                                  # no path avoids the stop
        ("0f6fc1", "movq mm0, mm1"),
        ("0f77", "emms"),
        ("c3", "ret"),
    ]),
    # The jump analysis walks back over a stop: vzeroupper writes no general register, so
    # 'jmp rax' is still the tail call through the pointer the mov loaded.
    "tail_fn": (0x10c0, [
        ("488b01", "mov rax, qword ptr [rcx]"),
        ("c5f877", "vzeroupper"),
        ("ffe0", "jmp rax"),
    ]),
    # The jump's register was last written by the instruction without a translation: where the
    # jump goes depends on what that instruction would have computed.  Skipped, as before.
    "writer_fn": (0x1100, [
        ("31c9", "xor ecx, ecx"),
        ("0f01d0", "xgetbv"),
        ("ffe0", "jmp rax"),
    ]),
    # An instruction that would be a stop, and a jump nobody can resolve: skipped for the jump.
    "mixed_fn": (0x1140, [
        ("c5f877", "vzeroupper"),
        ("488b04d1", "mov rax, qword ptr [rcx + rdx*8]"),
        ("ffe0", "jmp rax"),
    ]),
    # A switch with a stop in one of its cases: the table is recovered as ever (STOP_TABLE).
    "case_fn": (0x1180, [
        ("c1e904", "shr ecx, 4"),                         # bits 4 and 5: the test's pointers vary there
        ("83e103", "and ecx, 3"),
        ("488d15731e0000", "lea rdx, [rip + 0x1e73]"),    # 0x3000
        ("4863048a", "movsxd rax, dword ptr [rdx + rcx*4]"),
        ("4801d0", "add rax, rdx"),
        ("ffe0", "jmp rax"),
        ("b80a000000", "mov eax, 0xa"),                   # 0x1196: case 0
        ("c3", "ret"),
        ("c5f877", "vzeroupper"),                         # 0x119c: case 1, a stop
        ("b80b000000", "mov eax, 0xb"),
        ("c3", "ret"),
        ("b80c000000", "mov eax, 0xc"),                   # 0x11a5: case 2
        ("c3", "ret"),
        ("b80d000000", "mov eax, 0xd"),                   # 0x11ab: case 3
        ("c3", "ret"),
    ]),
    # Two bytes of this function's first instruction lie in a relocation slot that begins
    # before it (STOP_RELOCS): loading the image changes them.  Not a stop: skipped.
    "reloc_fn": (0x1200, [
        ("83c164", "add ecx, 0x64"),
        ("c3", "ret"),
    ]),
    "stop_caller": (0x1240, [                             # calls guard_fn
        ("4883ec28", "sub rsp, 0x28"),
        ("e8f7fdffff", "call 0x1040"),
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
    "stop_slot_caller": (0x1280, [                        # calls through [rdx]
        ("4883ec28", "sub rsp, 0x28"),
        ("ff12", "call qword ptr [rdx]"),
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
}
STOP_LABELS = {n: rva for n, (rva, _) in STOP_PROGRAMS.items()}
STOP_TABLE = (0x3000, 0x3000, [0x1196, 0x119c, 0x11a5, 0x11ab])     # (rva, entries are relative to, targets)
STOP_RELOCS = [0x1200 - 6]

# Where CPUID reports each feature named below, as LEAF.REG.BIT (leaf in hex): Intel SDM vol. 2A,
# CPUID, the tables of leaf 01H and of leaf 07H sub-leaf 0, and AMD's leaf 8000_0001H for XOP.
# runtime/core/machine.c names the same bits where it says which the runtime reports.  This is
# the test's own copy: the lifter's table (stops.CPUID_BITS) is checked against it.
STOP_CPUID = {"MMX": "1.edx.23", "PCLMULQDQ": "1.ecx.1", "MOVBE": "1.ecx.22", "POPCNT": "1.ecx.23", "AES": "1.ecx.25",
              "OSXSAVE": "1.ecx.27", "AVX": "1.ecx.28", "BMI1": "7.ebx.3", "AVX2": "7.ebx.5", "BMI2": "7.ebx.8",
              "ADX": "7.ebx.19", "SHA": "7.ebx.29", "XOP": "80000001.ecx.11", "LAHF-SAHF": "80000001.ecx.0"}

# (rva, hex, Capstone's text, the features a processor must report to execute it): each is the
# first instruction of a two-instruction function (it, then ret) in an image of their own.
STOP_VECTORS = [
    (0x1000, "0f01d0", "xgetbv", "OSXSAVE"),              # #UD unless the OS set CR4.OSXSAVE, which that bit shows
    (0x1020, "c5fd6fc1", "vmovdqa ymm0, ymm1", "AVX"),
    (0x1040, "c5f1efc2", "vpxor xmm0, xmm1, xmm2", "AVX"),
    (0x1060, "c5f5efc2", "vpxor ymm0, ymm1, ymm2", "AVX2"),            # the same instruction on ymm registers
    (0x1080, "c4e3710fc204", "vpalignr xmm0, xmm1, xmm2, 4", "AVX"),   # the decoder gives it no group: VEX-encoded
    (0x10a0, "c4e3750fc204", "vpalignr ymm0, ymm1, ymm2, 4", "AVX"),   # on vector registers, so AVX at least
    (0x10c0, "660f38dcc1", "aesenc xmm0, xmm1", "AES"),
    (0x10e0, "c4e271dcc2", "vaesenc xmm0, xmm1, xmm2", "AVX AES"),     # the VEX form waits for both
    (0x1100, "660f3a44c100", "pclmulqdq xmm0, xmm1, 0", "PCLMULQDQ"),
    (0x1120, "c4e37144c200", "vpclmulqdq xmm0, xmm1, xmm2, 0", "AVX PCLMULQDQ"),
    (0x1140, "0f38cbc1", "sha256rnds2 xmm0, xmm1, xmm0", "SHA"),
    (0x1160, "c4e2fbf6c3", "mulx rax, rax, rbx", "BMI2"),
    (0x1180, "c4e37bf0c105", "rorx eax, ecx, 5", "BMI2"),
    (0x11a0, "c4e278f2c1", "andn eax, eax, ecx", "BMI1"),
    (0x11c0, "0f38f001", "movbe eax, dword ptr [rcx]", "MOVBE"),
    (0x11e0, "660f38f6c1", "adcx eax, ecx", "ADX"),
    (0x1200, "f30fb8c1", "popcnt eax, ecx", "POPCNT"),
    (0x1220, "8fe878c2c105", "vprotd xmm0, xmm1, 5", "XOP"),           # AMD only
    (0x1240, "0f6fc1", "movq mm0, mm1", "MMX"),           # a feature every x86-64 processor has: the lifter's gap
    (0x1260, "0fefc1", "pxor mm0, mm1", "MMX"),
    (0x1280, "0f77", "emms", "MMX"),
    (0x12a0, "f30fbcc1", "tzcnt eax, ecx", ""),           # no feature on purpose: without BMI1 these bytes run as bsf,
    (0x12c0, "f3480fbdc1", "lzcnt rax, rcx", ""),         # and these as bsr: there is no fault for a stop to stand for
    (0x12e0, "f0f71b", "lock neg dword ptr [rbx]", ""),   # a plain instruction the lifter refuses (review finding 11)
    (0x1300, "8cd8", "mov eax, ds", ""),
    (0x1320, "d900", "fld dword ptr [rax]", ""),          # x87
    (0x1340, "f2a5", "movsd dword ptr [rdi], dword ptr [rsi]", ""),    # a prefix the decoder does not report (finding 6)
    (0x1360, "648b03", "mov eax, dword ptr fs:[rbx]", ""),             # refused for its operand, not its name
    (0x1660, "9f", "lahf", "LAHF-SAHF"),                  # in 64-bit mode only where CPUID says so
]

# (rva, hex, Capstone's text): the same arrangement; each must leave its function skipped, with
# the instruction as the reason.
STOP_REFUSED = [
    (0x1380, "48ff2b", "ljmp [rbx]"),                     # far jumps and calls
    (0x13a0, "ff2b", "jmp ptr [rbx]"),
    (0x13c0, "ff1b", "call ptr [rbx]"),
    (0x13e0, "48ff1b", "lcall [rbx]"),
    (0x1400, "cd2e", "int 0x2e"),                         # a software interrupt other than 0x29
    (0x1420, "0f05", "syscall"),
    (0x1440, "0f34", "sysenter"),
    (0x1460, "c7f800000000", "xbegin 0x1466"),            # a transaction has a second way out
    (0x1480, "c6f800", "xabort 0"),
    (0x14a0, "0f01d5", "xend"),
    (0x14c0, "0fb9", "ud1"),                              # always #UD; and the decoder takes two bytes where Intel's
    (0x14e0, "0fff", "ud0"),                              # UD0 and UD1 have a ModRM byte: not even the length is sure
    (0x1500, "e400", "in al, 0"),                         # I/O: in a user-mode module this is data read as code
    (0x1520, "fa", "cli"),                                # privileged
    (0x1540, "0f01f9", "rdtscp"),                         # not privileged, but the decoder says so and the fence is its group
    (0x1560, "0f01d1", "xsetbv"),
    (0x1580, "0f32", "rdmsr"),                            # privileged, and the decoder gives it no group: fenced by name
    (0x15a0, "0f01d7", "enclu"),                          # enters or leaves an enclave; no group either
    (0x15c0, "0f37", "getsec"),
    (0x15e0, "f1", "int1"),
    (0x1600, "66c3", "ret"),                              # near branches with a 0x66 prefix (review finding 13)
    (0x1620, "66e900000000", "jmp 0x1626"),
    (0x1640, "66ffe2", "jmp rdx"),
]
