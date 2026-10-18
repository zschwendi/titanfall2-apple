"""Self-test data: hand-sized x86-64 programs and single instructions as hex bytes with their
assembly.  No game files are involved.

This file: whole programs (PROGRAMS, JUMP_PROGRAMS, TAIL_PROGRAMS, LEA_PROGRAMS) and the single instructions
that test the core rather than a family (UNSUPPORTED_VECTORS, NAME_CLASH_VECTORS, ORACLE_BUG_VECTORS).

tests/vectors/<family>.py: the hand-written single-instruction vectors of the instruction family
lifter/insns/<family>.py.  They are found by listing this directory, never from a list:

    VECTORS = [(rva, hex, text), ...]         each must be translated and agree with the oracle
    REFUSED = [(rva, hex, text, why), ...]    optional: encodings the family must refuse; `why`
                                              is a substring of the reason the lifter gives

`text` is the assembly exactly as the decoder prints it; run_selftests.py checks it against the
bytes.  Every mnemonic a family registers needs at least one entry in VECTORS.

Each family's vectors live in an image of their own (run_selftests.insn_image), so one family's
addresses never clash with another's:

    0x0f00            a branch may target this address (nothing is there)
    0x1000 - 0x4ff0   one instruction per 16-byte slot; `rva` is the slot
    0x8000 - 0x81ff   data, for RIP-relative operands: byte i holds (i * 167 + 13) & 0xff
"""

import importlib
import pkgutil

PROGRAMS = {
    "sum_loop": (0x1000, [
        ("83e13f", "and ecx, 0x3f"),
        ("31c0", "xor eax, eax"),
        ("85c9", "test ecx, ecx"),
        ("7e06", "jle 0x100f"),
        ("01c8", "add eax, ecx"),
        ("ffc9", "dec ecx"),
        ("75fa", "jne 0x1009"),
        ("c3", "ret"),
    ]),
    "calls_out": (0x1040, [
        ("53", "push rbx"),
        ("4883ec20", "sub rsp, 0x20"),
        ("4889cb", "mov rbx, rcx"),
        ("e833000000", "call 0x1080"),
        ("4801c3", "add rbx, rax"),
        ("4889d9", "mov rcx, rbx"),
        ("ff15a72f0000", "call qword ptr [rip + 0x2fa7]"),
        ("488d0520000000", "lea rax, [rip + 0x20]"),
        ("ffd0", "call rax"),
        ("4889d8", "mov rax, rbx"),
        ("4883c420", "add rsp, 0x20"),
        ("5b", "pop rbx"),
        ("c3", "ret"),
    ]),
    "helper": (0x1080, [
        ("8d444907", "lea eax, [rcx + rcx*2 + 7]"),
        ("c3", "ret"),
    ]),
    "cond_tail": (0x10c0, [
        ("f6c101", "test cl, 1"),
        ("75bb", "jne 0x1080"),
        ("83fa05", "cmp edx, 5"),
        ("7206", "jb 0x10d0"),
        ("ff25382f0000", "jmp qword ptr [rip + 0x2f38]"),
        ("89d0", "mov eax, edx"),
        ("ebac", "jmp 0x1080"),
    ]),
    "reg_writes": (0x1100, [
        ("48b88877665544332211", "movabs rax, 0x1122334455667788"),
        ("b099", "mov al, 0x99"),
        ("88cc", "mov ah, cl"),
        ("4989c0", "mov r8, rax"),
        ("b8efbeadde", "mov eax, 0xdeadbeef"),
        ("4989c1", "mov r9, rax"),
        ("48c7c2ffffffff", "mov rdx, 0xffffffffffffffff"),
        ("66ba3412", "mov dx, 0x1234"),
        ("88ea", "mov dl, ch"),
        ("0fb6f4", "movzx esi, ah"),
        ("4c0fbed8", "movsx r11, al"),
        ("86c4", "xchg ah, al"),
        ("80c47f", "add ah, 0x7f"),
        ("0f90c1", "seto cl"),
        ("10ed", "adc ch, ch"),
        ("664189c0", "mov r8w, ax"),
        ("4188d1", "mov r9b, dl"),
        ("4183caff", "or r10d, 0xffffffff"),
        ("c3", "ret"),
    ]),
    "stack_ops": (0x1180, [
        ("55", "push rbp"),
        ("4889e5", "mov rbp, rsp"),
        ("51", "push rcx"),
        ("6878563412", "push 0x12345678"),
        ("6afe", "push -2"),
        ("ff75f8", "push qword ptr [rbp - 8]"),
        ("58", "pop rax"),
        ("5a", "pop rdx"),
        ("8f442408", "pop qword ptr [rsp + 8]"),
        ("54", "push rsp"),
        ("4158", "pop r8"),
        ("c9", "leave"),
        ("c21000", "ret 0x10"),
    ]),
    "flags_mix": (0x11c0, [
        ("89c8", "mov eax, ecx"),
        ("01d0", "add eax, edx"),
        ("410f92c0", "setb r8b"),
        ("410f90c1", "seto r9b"),
        ("4d19d2", "sbb r10, r10"),
        ("f7da", "neg edx"),
        ("83d000", "adc eax, 0"),
        ("88d1", "mov cl, dl"),
        ("48d3e0", "shl rax, cl"),
        ("c1f803", "sar eax, 3"),
        ("66d3c0", "rol ax, cl"),
        ("0fbae207", "bt edx, 7"),
        ("4c0f42d8", "cmovb r11, rax"),
        ("4538c8", "cmp r8b, r9b"),
        ("0f4ec2", "cmovle eax, edx"),
        ("486bc235", "imul rax, rdx, 0x35"),
        ("c3", "ret"),
    ]),
    "mem_ops": (0x1240, [
        ("65488b042558000000", "mov rax, qword ptr gs:[0x58]"),
        ("4c8b4810", "mov r9, qword ptr [rax + 0x10]"),
        ("66894822", "mov word ptr [rax + 0x22], cx"),
        ("488b15a81d0000", "mov rdx, qword ptr [rip + 0x1da8]"),
        ("480115a91d0000", "add qword ptr [rip + 0x1da9], rdx"),
        ("49b80830008001000000", "movabs r8, 0x180003008"),
        ("4d8b18", "mov r11, qword ptr [r8]"),
        ("4c8b15a51d0000", "mov r10, qword ptr [rip + 0x1da5]"),
        ("4d8b12", "mov r10, qword ptr [r10]"),
        ("f00fc10d921d0000", "lock xadd dword ptr [rip + 0x1d92], ecx"),
        ("4889d0", "mov rax, rdx"),
        ("f0480fb10d7e1d0000", "lock cmpxchg qword ptr [rip + 0x1d7e], rcx"),
        ("4c871d771d0000", "xchg qword ptr [rip + 0x1d77], r11"),
        ("c3", "ret"),
    ]),
    "sse_moves": (0x12c0, [
        ("0f28f0", "movaps xmm6, xmm0"),
        ("f30f100d351d0000", "movss xmm1, dword ptr [rip + 0x1d35]"),
        ("f20f1015351d0000", "movsd xmm2, qword ptr [rip + 0x1d35]"),
        ("f30f10d9", "movss xmm3, xmm1"),
        ("f20f10e2", "movsd xmm4, xmm2"),
        ("660f6ee9", "movd xmm5, ecx"),
        ("66480f6efa", "movq xmm7, rdx"),
        ("660f7ec0", "movd eax, xmm0"),
        ("66490f7ec8", "movq r8, xmm1"),
        ("660fefc0", "pxor xmm0, xmm0"),
        ("0f57ce", "xorps xmm1, xmm6"),
        ("0f54d6", "andps xmm2, xmm6"),
        ("0f561d021d0000", "orps xmm3, xmmword ptr [rip + 0x1d02]"),
        ("0f117424d8", "movups xmmword ptr [rsp - 0x28], xmm6"),
        ("66440f6f4424d8", "movdqa xmm8, xmmword ptr [rsp - 0x28]"),
        ("f3440f6f4c24d9", "movdqu xmm9, xmmword ptr [rsp - 0x27]"),
        ("f30f116c24c0", "movss dword ptr [rsp - 0x40], xmm5"),
        ("f20f117c24b8", "movsd qword ptr [rsp - 0x48], xmm7"),
        ("f3450f7ed1", "movq xmm10, xmm9"),
        ("66440fd65424b0", "movq qword ptr [rsp - 0x50], xmm10"),
        ("c3", "ret"),
    ]),
    "string_ops": (0x1380, [
        ("57", "push rdi"),
        ("56", "push rsi"),
        ("488dbc2400ffffff", "lea rdi, [rsp - 0x100]"),
        ("b921000000", "mov ecx, 0x21"),
        ("b841000000", "mov eax, 0x41"),
        ("f3aa", "rep stosb byte ptr [rdi], al"),
        ("488db42400ffffff", "lea rsi, [rsp - 0x100]"),
        ("488dbc2400feffff", "lea rdi, [rsp - 0x200]"),
        ("b904000000", "mov ecx, 4"),
        ("f348a5", "rep movsq qword ptr [rdi], qword ptr [rsi]"),
        ("a4", "movsb byte ptr [rdi], byte ptr [rsi]"),
        ("488dbc2400ffffff", "lea rdi, [rsp - 0x100]"),
        ("b940000000", "mov ecx, 0x40"),
        ("b042", "mov al, 0x42"),
        ("f2ae", "repne scasb al, byte ptr [rdi]"),
        ("4889ca", "mov rdx, rcx"),
        ("488db42400ffffff", "lea rsi, [rsp - 0x100]"),
        ("488dbc2400feffff", "lea rdi, [rsp - 0x200]"),
        ("b930000000", "mov ecx, 0x30"),
        ("f3a6", "repe cmpsb byte ptr [rsi], byte ptr [rdi]"),
        ("0f95c0", "setne al"),
        ("5e", "pop rsi"),
        ("5f", "pop rdi"),
        ("c3", "ret"),
    ]),
    "split_main": (0x1440, [
        ("85c9", "test ecx, ecx"),
        ("743c", "je 0x1480"),
        ("b801000000", "mov eax, 1"),
        ("c3", "ret"),
    ]),
    "split_frag": (0x1480, [
        ("b802000000", "mov eax, 2"),
        ("ebc2", "jmp 0x1449"),
    ]),
    "switch_fn": (0x14c0, [
        ("4c8d0539ebffff", "lea r8, [rip - 0x14c7]"),
        ("83e107", "and ecx, 7"),
        ("83f903", "cmp ecx, 3"),
        ("7728", "ja 0x14f7"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b948040300000", "mov edx, dword ptr [r8 + rax*4 + 0x3040]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b80a000000", "mov eax, 0xa"),
        ("c3", "ret"),
        ("b80b000000", "mov eax, 0xb"),
        ("c3", "ret"),
        ("b80c000000", "mov eax, 0xc"),
        ("c3", "ret"),
        ("b80d000000", "mov eax, 0xd"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    "noret_fn": (0x1540, [
        ("4883ec28", "sub rsp, 0x28"),
        ("e837fbffff", "call 0x1080"),
    ]),
    "fall_a": (0x1580, [
        ("ffc1", "inc ecx"),
    ]),
    "fall_b": (0x1582, [
        ("89c8", "mov eax, ecx"),
        ("c3", "ret"),
    ]),
    "div_fn": (0x15c0, [
        ("89c8", "mov eax, ecx"),
        ("81e2ff000000", "and edx, 0xff"),
        ("83ca01", "or edx, 1"),
        ("4189d0", "mov r8d, edx"),
        ("31d2", "xor edx, edx"),
        ("41f7f0", "div r8d"),
        ("4189d1", "mov r9d, edx"),
        ("4863c1", "movsxd rax, ecx"),
        ("4899", "cqo"),
        ("49f7f8", "idiv r8"),
        ("c3", "ret"),
    ]),
    # Values set up only to be passed to a callee: rax, r10, xmm1, xmm2 (review finding 3).
    "args_fn": (0x1600, [
        ("4883ec28", "sub rsp, 0x28"),
        ("f30f100df4190000", "movss xmm1, dword ptr [rip + 0x19f4]"),
        ("0f28d7", "movaps xmm2, xmm7"),
        ("b805000000", "mov eax, 5"),
        ("4989ca", "mov r10, rcx"),
        ("e864faffff", "call 0x1080"),
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
    # The second call overwrites the stack slot that held the first call's return address.
    "twocalls_fn": (0x1640, [
        ("4883ec28", "sub rsp, 0x28"),
        ("e837faffff", "call 0x1080"),
        ("e832faffff", "call 0x1080"),
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
    # Defined guest faults (review finding 4): #DE for most inputs, and a path that ends in int3.
    "div_trap_fn": (0x1680, [
        ("f7f1", "div ecx"),
        ("c3", "ret"),
    ]),
    "trap_fn": (0x16a0, [
        ("b807000000", "mov eax, 7"),
        ("cc", "int3"),
    ]),
    # Coverage and seed families (review finding 1, issue 44).
    # cov_fn: the branch can never be taken, so two of its five instructions never run.
    "cov_fn": (0x16c0, [
        ("4839c9", "cmp rcx, rcx"),
        ("7501", "jne 0x16c6"),
        ("c3", "ret"),
        ("b801000000", "mov eax, 1"),
        ("c3", "ret"),
    ]),
    # fourargs_fn: runs only when all four argument registers are valid pointers.
    "fourargs_fn": (0x1700, [
        ("488b01", "mov rax, qword ptr [rcx]"),
        ("480302", "add rax, qword ptr [rdx]"),
        ("490300", "add rax, qword ptr [r8]"),
        ("490301", "add rax, qword ptr [r9]"),
        ("c3", "ret"),
    ]),
    # gptr_fn: dereferences a global pointer (gptr, 0x3020) that is null in the image.
    "gptr_fn": (0x1740, [
        ("488b05d9180000", "mov rax, qword ptr [rip + 0x18d9]"),
        ("8b00", "mov eax, dword ptr [rax]"),
        ("c3", "ret"),
    ]),
    # memint_fn: branches on an integer loaded from memory; half of a pointer never has the
    # upper 16 bits of its low dword clear.
    "memint_fn": (0x1780, [
        ("f7010000ffff", "test dword ptr [rcx], 0xffff0000"),
        ("7406", "je 0x178e"),
        ("b801000000", "mov eax, 1"),
        ("c3", "ret"),
        ("b802000000", "mov eax, 2"),
        ("c3", "ret"),
    ]),
    # A jmp whose target is the caller's return address: a tail call to that address, not a
    # return (review finding 13).  It leaves through a copy of the return address it pushed.
    "jmp_rsp_fn": (0x17c0, [
        ("488b0424", "mov rax, qword ptr [rsp]"),
        ("50", "push rax"),
        ("ff2424", "jmp qword ptr [rsp]"),
    ]),
    # Demand pages (issue 44, proposal 3): memory outside the image, arena and stack.
    # far_read_fn: loads a pointer 2 MiB past its argument, off the arena, and follows it.
    "far_read_fn": (0x1800, [
        ("488b8100002000", "mov rax, qword ptr [rcx + 0x200000]"),
        ("488b00", "mov rax, qword ptr [rax]"),
        ("c3", "ret"),
    ]),
    # far_write_fn: stores off the arena and reads the qword after it.
    "far_write_fn": (0x1840, [
        ("48899100103000", "mov qword ptr [rcx + 0x301000], rdx"),
        ("488b8108103000", "mov rax, qword ptr [rcx + 0x301008]"),
        ("c3", "ret"),
    ]),
    # imp_data_fn: an imported variable: the import slot (imp1) holds its address.
    "imp_data_fn": (0x1880, [
        ("488b0581270000", "mov rax, qword ptr [rip + 0x2781]"),
        ("488b00", "mov rax, qword ptr [rax]"),
        ("8b00", "mov eax, dword ptr [rax]"),
        ("c3", "ret"),
    ]),
    # noncanon_fn: an address that is not canonical, whose low 52 bits are an address 3 MiB
    # past the arena.  The emulator looks memory up by those 52 bits; a CPU raises #GP.
    "noncanon_fn": (0x18c0, [
        ("48b800003000002190f2", "movabs rax, 0xf290210000300000"),
        ("488b00", "mov rax, qword ptr [rax]"),
        ("c3", "ret"),
    ]),
    # page_walk_fn: touches 300 pages, more than a case may map.
    "page_walk_fn": (0x1900, [
        ("b82c010000", "mov eax, 0x12c"),
        ("4c8b8200004000", "mov r8, qword ptr [rdx + 0x400000]"),
        ("4881c200100000", "add rdx, 0x1000"),
        ("ffc8", "dec eax"),
        ("75ee", "jne 0x1905"),
        ("c3", "ret"),
    ]),
    # Limits (issue 44, proposal 4).  call_loop_fn never returns: it fills the call record.
    # rsi counts the iterations and reaches no callee.
    "call_loop_fn": (0x1940, [
        ("4801c3", "add rbx, rax"),
        ("48ffc6", "inc rsi"),
        ("4889d9", "mov rcx, rbx"),
        ("ff15b1260000", "call qword ptr [rip + 0x26b1]"),
        ("ebef", "jmp 0x1940"),
    ]),
    # count_loop_fn: 104,002 instructions between two calls, so the second call comes after
    # the instruction limit (200,000) and within the grace allowed to reach an event.
    "count_loop_fn": (0x1980, [
        ("b920cb0000", "mov ecx, 0xcb20"),
        ("ffc9", "dec ecx"),
        ("75fc", "jne 0x1985"),
        ("48ffc6", "inc rsi"),
        ("ff156e260000", "call qword ptr [rip + 0x266e]"),
        ("ebec", "jmp 0x1980"),
    ]),
    # spin_fn: a loop with no event in it; nothing but the instruction limit ends it.
    "spin_fn": (0x19c0, [
        ("ffc0", "inc eax"),
        ("ebfc", "jmp 0x19c0"),
    ]),
    # The stack probe.  probe_fn has 0x1040 bytes of locals: it calls the probe and takes rax,
    # which the probe leaves alone, off rsp.  probe_stub_fn stands for the probe (only its
    # callers make it one).  probe_ok_fn puts back what it took, whatever rax is.
    "probe_fn": (0x1a00, [
        ("b840100000", "mov eax, 0x1040"),
        ("e836000000", "call 0x1a40"),
        ("482be0", "sub rsp, rax"),
        ("48894c2420", "mov qword ptr [rsp + 0x20], rcx"),
        ("488b442420", "mov rax, qword ptr [rsp + 0x20]"),
        ("4881c440100000", "add rsp, 0x1040"),
        ("c3", "ret"),
    ]),
    "probe_stub_fn": (0x1a40, [
        ("c3", "ret"),
    ]),
    "probe_ok_fn": (0x1a60, [
        ("e8dbffffff", "call 0x1a40"),
        ("482be0", "sub rsp, rax"),
        ("4803e0", "add rsp, rax"),
        ("c3", "ret"),
    ]),
}

# Indirect jumps through a register (review finding 10 and the 'jmp reg' rule); an image of
# their own (run_selftests.jump_image).  jtab_abs is two absolute pointers at 0x3100.
# (The index is taken from bit 4 up: the low bits of the harness's seed values are not random.)
JUMP_PROGRAMS = {
    # an indexed load of a code pointer from a table of absolute addresses (jtab_abs): two cases
    "abs_switch": (0x1000, [
        ("488d05f9200000", "lea rax, [rip + 0x20f9]"),
        ("c1e904", "shr ecx, 4"),
        ("83e101", "and ecx, 1"),
        ("488b04c8", "mov rax, qword ptr [rax + rcx*8]"),
        ("ffe0", "jmp rax"),
        ("b80a000000", "mov eax, 0xa"),
        ("c3", "ret"),
        ("b80b000000", "mov eax, 0xb"),
        ("c3", "ret"),
    ]),
    # jumps to the address a helper returned: a tail call
    "thunk_call": (0x1040, [
        ("4883ec28", "sub rsp, 0x28"),
        ("e877000000", "call 0x10c0"),
        ("4883c428", "add rsp, 0x28"),
        ("ffe0", "jmp rax"),
    ]),
    # a pointer loaded from a slot without an index: a tail call
    "slot_jmp": (0x1060, [
        ("488b4108", "mov rax, qword ptr [rcx + 8]"),
        ("ffe0", "jmp rax"),
    ]),
    # the loaded pointer is modified before the jump -> skipped
    "xor_jmp": (0x1080, [
        ("488b05791f0000", "mov rax, qword ptr [rip + 0x1f79]"),
        ("4833c1", "xor rax, rcx"),
        ("ffe0", "jmp rax"),
    ]),
    # a pointer loaded from one slot of the image and decoded with a key from another, both
    # without a register in the address (the C runtime's encoded function pointers): a tail call
    "decode_jmp": (0x1600, [
        ("488b05f9190000", "mov rax, qword ptr [rip + 0x19f9]"),
        ("483305fa190000", "xor rax, qword ptr [rip + 0x19fa]"),
        ("7402", "je 0x1612"),
        ("ffe0", "jmp rax"),
        ("b807000000", "mov eax, 7"),
        ("c3", "ret"),
    ]),
    # the same with a frame, and the pointer and the key in one slot: the decoded pointer is
    # null and the fallback runs
    "decode_null": (0x1640, [
        ("4883ec28", "sub rsp, 0x28"),
        ("488b05b5190000", "mov rax, qword ptr [rip + 0x19b5]"),
        ("483305ae190000", "xor rax, qword ptr [rip + 0x19ae]"),
        ("7406", "je 0x165a"),
        ("4883c428", "add rsp, 0x28"),
        ("ffe0", "jmp rax"),
        ("b808000000", "mov eax, 8"),
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
    # the pointer comes from an indexed load before it is decoded: a table -> skipped
    "decode_indexed": (0x1680, [
        ("488b04d1", "mov rax, qword ptr [rcx + rdx*8]"),
        ("4833057d190000", "xor rax, qword ptr [rip + 0x197d]"),
        ("ffe0", "jmp rax"),
    ]),
    # decoded with a key found through a register: not the idiom -> skipped
    "decode_regkey": (0x16a0, [
        ("488b0559190000", "mov rax, qword ptr [rip + 0x1959]"),
        ("48334108", "xor rax, qword ptr [rcx + 8]"),
        ("ffe0", "jmp rax"),
    ]),
    # rcx after a call holds whatever the callee left there -> skipped
    "call_rcx": (0x10a0, [
        ("4883ec28", "sub rsp, 0x28"),
        ("e817000000", "call 0x10c0"),
        ("4883c428", "add rsp, 0x28"),
        ("ffe1", "jmp rcx"),
    ]),
    "helper": (0x10c0, [
        ("c3", "ret"),
    ]),
    # Jump tables.  clang layout: 32-bit entries relative to the table, bounds check on the index.
    "clang_switch": (0x1100, [
        ("83f903", "cmp ecx, 3"),
        ("772a", "ja 0x112f"),
        ("89c9", "mov ecx, ecx"),
        ("4c8d05f2200000", "lea r8, [rip + 0x20f2]"),
        ("49630488", "movsxd rax, dword ptr [r8 + rcx*4]"),
        ("4c01c0", "add rax, r8"),
        ("ffe0", "jmp rax"),
        ("b80a000000", "mov eax, 0xa"),
        ("c3", "ret"),
        ("b80b000000", "mov eax, 0xb"),
        ("c3", "ret"),
        ("b80c000000", "mov eax, 0xc"),
        ("c3", "ret"),
        ("b80d000000", "mov eax, 0xd"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... the index bounded by a mask instead of a compare
    "and_switch": (0x1180, [
        ("c1e904", "shr ecx, 4"),
        ("83e103", "and ecx, 3"),
        ("488d1593200000", "lea rdx, [rip + 0x2093]"),
        ("48630c8a", "movsxd rcx, dword ptr [rdx + rcx*4]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b814000000", "mov eax, 0x14"),
        ("c3", "ret"),
        ("b815000000", "mov eax, 0x15"),
        ("c3", "ret"),
        ("b816000000", "mov eax, 0x16"),
        ("c3", "ret"),
        ("b817000000", "mov eax, 0x17"),
        ("c3", "ret"),
    ]),
    # Microsoft layout (entries are RVAs), with an unrelated 'cmp edx, 1; ja' between the real
    # bounds check and the load: the table has 6 entries, not 2
    "two_checks": (0x1200, [
        ("4c8d05f9edffff", "lea r8, [rip - 0x1207]"),
        ("c1e904", "shr ecx, 4"),
        ("83e107", "and ecx, 7"),
        ("83e201", "and edx, 1"),
        ("83f905", "cmp ecx, 5"),
        ("7739", "ja 0x124e"),
        ("83fa01", "cmp edx, 1"),
        ("7734", "ja 0x124e"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b948040320000", "mov edx, dword ptr [r8 + rax*4 + 0x3240]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b81e000000", "mov eax, 0x1e"),
        ("c3", "ret"),
        ("b81f000000", "mov eax, 0x1f"),
        ("c3", "ret"),
        ("b820000000", "mov eax, 0x20"),
        ("c3", "ret"),
        ("b821000000", "mov eax, 0x21"),
        ("c3", "ret"),
        ("b822000000", "mov eax, 0x22"),
        ("c3", "ret"),
        ("b823000000", "mov eax, 0x23"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... the lea of the table is not in the straight-line run that ends in the jump
    "hoisted_switch": (0x1280, [
        ("4c8d0dd91f0000", "lea r9, [rip + 0x1fd9]"),
        ("eb02", "jmp 0x128b"),
        ("cc", "int3"),
        ("cc", "int3"),
        ("c1e904", "shr ecx, 4"),
        ("83e103", "and ecx, 3"),
        ("49630489", "movsxd rax, dword ptr [r9 + rcx*4]"),
        ("4c01c8", "add rax, r9"),
        ("ffe0", "jmp rax"),
        ("b828000000", "mov eax, 0x28"),
        ("c3", "ret"),
        ("b829000000", "mov eax, 0x29"),
        ("c3", "ret"),
        ("b82a000000", "mov eax, 0x2a"),
        ("c3", "ret"),
        ("b82b000000", "mov eax, 0x2b"),
        ("c3", "ret"),
    ]),
    # ... the base register can hold either of two tables: must be skipped
    "ambig_switch": (0x1300, [
        ("85d2", "test edx, edx"),
        ("7409", "je 0x130d"),
        ("4c8d0d751f0000", "lea r9, [rip + 0x1f75]"),
        ("eb07", "jmp 0x1314"),
        ("4c8d0d7c1f0000", "lea r9, [rip + 0x1f7c]"),
        ("c1e904", "shr ecx, 4"),
        ("83e101", "and ecx, 1"),
        ("49630489", "movsxd rax, dword ptr [r9 + rcx*4]"),
        ("4c01c8", "add rax, r9"),
        ("ffe0", "jmp rax"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("b833000000", "mov eax, 0x33"),
        ("c3", "ret"),
    ]),
    # Code inside a function's unwind range that nothing branches to (review finding 9): the
    # two instructions after the jmp stand for an exception handler; the int3 and nop are padding.
    "handler_fn": (0x1380, [
        ("b801000000", "mov eax, 1"),
        ("eb0a", "jmp 0x1391"),
        ("b802000000", "mov eax, 2"),
        ("c3", "ret"),
        ("cc", "int3"),
        ("0f1f00", "nop dword ptr [rax]"),
        ("c3", "ret"),
    ]),
    # ... and the same shape with padding only
    "padded_fn": (0x13c0, [
        ("b801000000", "mov eax, 1"),
        ("eb04", "jmp 0x13cb"),
        ("cc", "int3"),
        ("0f1f00", "nop dword ptr [rax]"),
        ("c3", "ret"),
    ]),
    # Tables of absolute addresses (JUMP_ABS_TABLES), clang's 'jmp [base + index*8]' (issue 13).
    # An indexed load through a pointer nobody can read.  Review finding 10 had it skipped: an
    # indexed load is a table, and a table is read or the function is skipped, never taken as a
    # tail call with the cases missing.  Since unit lifter-tail-tables the line is drawn by what
    # the table can be.  A table whose address the file fixes (a lea of read-only bytes, a
    # read-only slot) can be one of the function's own switch tables and is still read, with a
    # bound, or the function skipped: unb_switch, norel_switch, tail_roslot.  A table behind a
    # pointer that exists only at run time (here the caller's, in rcx) cannot be: a compiler takes
    # the address of a switch table as a link-time constant.  So this jump is an indirect tail
    # call through the dispatcher, which stops on an address that is no lifted function, taken
    # because the stack is shown to be at its entry level (the function never touches it).
    "rt_switch": (0x1400, [
        ("83e201", "and edx, 1"),
        ("488b04d1", "mov rax, qword ptr [rcx + rdx*8]"),
        ("ffe0", "jmp rax"),
    ]),
    # a computed goto in a leaf: the four labels are also discovery roots, because the table
    # slots are relocated pointers into code
    "goto_fn": (0x1420, [
        ("c1e904", "shr ecx, 4"),
        ("83e103", "and ecx, 3"),
        ("488d15d31e0000", "lea rdx, [rip + 0x1ed3]"),
        ("ff24ca", "jmp qword ptr [rdx + rcx*8]"),
        ("b83c000000", "mov eax, 0x3c"),
        ("c3", "ret"),
        ("b83d000000", "mov eax, 0x3d"),
        ("c3", "ret"),
        ("b83e000000", "mov eax, 0x3e"),
        ("c3", "ret"),
        ("b83f000000", "mov eax, 0x3f"),
        ("c3", "ret"),
    ]),
    # a call in tail position through a constant array of two functions (helper, slot_jmp)
    "fnptr_tail": (0x1460, [
        ("83e101", "and ecx, 1"),
        ("488d05d61e0000", "lea rax, [rip + 0x1ed6]"),
        ("ff24c8", "jmp qword ptr [rax + rcx*8]"),
    ]),
    # ... the table is in a writable section: must be skipped
    "wr_switch": (0x1480, [
        ("83e101", "and ecx, 1"),
        ("488d15f61b0000", "lea rdx, [rip + 0x1bf6]"),
        ("ff24ca", "jmp qword ptr [rdx + rcx*8]"),
        ("b846000000", "mov eax, 0x46"),
        ("c3", "ret"),
        ("b847000000", "mov eax, 0x47"),
        ("c3", "ret"),
    ]),
    # ... the second slot has no relocation entry: must be skipped
    "norel_switch": (0x14c0, [
        ("83e101", "and ecx, 1"),
        ("488d15b61e0000", "lea rdx, [rip + 0x1eb6]"),
        ("ff24ca", "jmp qword ptr [rdx + rcx*8]"),
        ("b850000000", "mov eax, 0x50"),
        ("c3", "ret"),
        ("b851000000", "mov eax, 0x51"),
        ("c3", "ret"),
    ]),
    # ... nothing bounds the index: must be skipped
    "unb_switch": (0x1500, [
        ("89c9", "mov ecx, ecx"),
        ("488d15b71e0000", "lea rdx, [rip + 0x1eb7]"),
        ("ff24ca", "jmp qword ptr [rdx + rcx*8]"),
        ("b85a000000", "mov eax, 0x5a"),
        ("c3", "ret"),
        ("b85b000000", "mov eax, 0x5b"),
        ("c3", "ret"),
    ]),
    # clang layout again; the base register held another address earlier in the function, and
    # the lea of the table is in the run that ends in the jump
    "two_lea_switch": (0x1540, [
        ("c1e904", "shr ecx, 4"),
        ("488d05b61a0000", "lea rax, [rip + 0x1ab6]"),
        ("4889442408", "mov qword ptr [rsp + 8], rax"),
        ("83e103", "and ecx, 3"),
        ("488d05a71e0000", "lea rax, [rip + 0x1ea7]"),
        ("48630c88", "movsxd rcx, dword ptr [rax + rcx*4]"),
        ("4801c1", "add rcx, rax"),
        ("ffe1", "jmp rcx"),
        ("b864000000", "mov eax, 0x64"),
        ("c3", "ret"),
        ("b865000000", "mov eax, 0x65"),
        ("c3", "ret"),
        ("b866000000", "mov eax, 0x66"),
        ("c3", "ret"),
        ("b867000000", "mov eax, 0x67"),
        ("c3", "ret"),
    ]),
    # 'jmp reg' judged over the whole function (the run that ends in the jump starts at a label).
    # The pointer is loaded without an index, tested, and jumped to from the other side of the
    # branch (the C runtime's setjmp, and methods called through a table of callbacks): a tail call
    "branch_tail": (0x1700, [
        ("488b4130", "mov rax, qword ptr [rcx + 0x30]"),
        ("4885c0", "test rax, rax"),
        ("7503", "jne 0x170c"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
        ("ffe0", "jmp rax"),
    ]),
    # ... one way to the jump loads the pointer whole, the other with an index: must be skipped
    "branch_mixed": (0x1720, [
        ("f6c210", "test dl, 0x10"),
        ("7406", "je 0x172b"),
        ("488b4108", "mov rax, qword ptr [rcx + 8]"),
        ("eb04", "jmp 0x172f"),
        ("488b04d1", "mov rax, qword ptr [rcx + rdx*8]"),
        ("ffe0", "jmp rax"),
    ]),
    # the caller's value: an argument copied to rax and jumped to, never written in between: a tail call
    "arg_tail": (0x1740, [
        ("4889d0", "mov rax, rdx"),
        ("4885d2", "test rdx, rdx"),
        ("7406", "je 0x174e"),
        ("488b5108", "mov rdx, qword ptr [rcx + 8]"),
        ("ffe0", "jmp rax"),
        ("c3", "ret"),
    ]),
    # ... a function that is nothing but the jump (what the end of a delay-load thunk becomes when
    # its unwind range stops one instruction early)
    "rax_stub": (0x1760, [
        ("ffe0", "jmp rax"),
    ]),
    # ... through a register the caller did not hand over (not volatile): must be skipped
    "rbx_stub": (0x1768, [
        ("ffe3", "jmp rbx"),
    ]),
    # one of two code addresses, both taken with a lea and chosen by a cmov (a routine picked by a
    # CPU feature or by alignment): the targets are known, here two functions -> a tail call
    "cmov_tail": (0x1780, [
        ("488d1539f9ffff", "lea rdx, [rip - 0x6c7]"),
        ("f6c110", "test cl, 0x10"),
        ("488d05cff8ffff", "lea rax, [rip - 0x731]"),
        ("480f45c2", "cmovne rax, rdx"),
        ("ffe0", "jmp rax"),
    ]),
    # ... both addresses inside the function: two labels, translated as a switch
    "cmov_local": (0x17a0, [
        ("488d1510000000", "lea rdx, [rip + 0x10]"),
        ("f6c110", "test cl, 0x10"),
        ("488d050c000000", "lea rax, [rip + 0xc]"),
        ("480f45c2", "cmovne rax, rdx"),
        ("ffe0", "jmp rax"),
        ("b86e000000", "mov eax, 0x6e"),
        ("c3", "ret"),
        ("b86f000000", "mov eax, 0x6f"),
        ("c3", "ret"),
    ]),
    # ... one inside and one outside: must be skipped
    "cmov_mixed": (0x17d0, [
        ("488d1510000000", "lea rdx, [rip + 0x10]"),
        ("f6c110", "test cl, 0x10"),
        ("488d05dff8ffff", "lea rax, [rip - 0x721]"),
        ("480f45c2", "cmovne rax, rdx"),
        ("ffe0", "jmp rax"),
        ("b870000000", "mov eax, 0x70"),
        ("c3", "ret"),
    ]),
    # Microsoft layout where the load overwrites its own base register and the add takes the image
    # base from another register, set by a lea of its own just before
    "two_reg_switch": (0x1800, [
        ("c1e904", "shr ecx, 4"),
        ("83e107", "and ecx, 7"),
        ("83f903", "cmp ecx, 3"),
        ("7734", "ja 0x183f"),
        ("488d15eee7ffff", "lea rdx, [rip - 0x1812]"),
        ("89c8", "mov eax, ecx"),
        ("8b948200350000", "mov edx, dword ptr [rdx + rax*4 + 0x3500]"),
        ("488d05dee7ffff", "lea rax, [rip - 0x1822]"),
        ("4801c2", "add rdx, rax"),
        ("ffe2", "jmp rdx"),
        ("b878000000", "mov eax, 0x78"),
        ("c3", "ret"),
        ("b879000000", "mov eax, 0x79"),
        ("c3", "ret"),
        ("b87a000000", "mov eax, 0x7a"),
        ("c3", "ret"),
        ("b87b000000", "mov eax, 0x7b"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... the other register is not set by a lea (its value is not known): must be skipped
    "two_reg_bad": (0x1880, [
        ("c1e904", "shr ecx, 4"),
        ("83e107", "and ecx, 7"),
        ("83f903", "cmp ecx, 3"),
        ("7730", "ja 0x18bb"),
        ("488d156ee7ffff", "lea rdx, [rip - 0x1892]"),
        ("89c8", "mov eax, ecx"),
        ("8b948220350000", "mov edx, dword ptr [rdx + rax*4 + 0x3520]"),
        ("4c89c8", "mov rax, r9"),
        ("4801c2", "add rdx, rax"),
        ("ffe2", "jmp rdx"),
        ("b882000000", "mov eax, 0x82"),
        ("c3", "ret"),
        ("b883000000", "mov eax, 0x83"),
        ("c3", "ret"),
        ("b884000000", "mov eax, 0x84"),
        ("c3", "ret"),
        ("b885000000", "mov eax, 0x85"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... 'add base, entry ; jmp base': the operands of the add the other way round (hand-written code)
    "swapped_switch": (0x1900, [
        ("c1e904", "shr ecx, 4"),
        ("83e107", "and ecx, 7"),
        ("83f903", "cmp ecx, 3"),
        ("772f", "ja 0x193a"),
        ("4c8d0deee6ffff", "lea r9, [rip - 0x1912]"),
        ("89c9", "mov ecx, ecx"),
        ("418b848940350000", "mov eax, dword ptr [r9 + rcx*4 + 0x3540]"),
        ("4901c1", "add r9, rax"),
        ("41ffe1", "jmp r9"),
        ("b88c000000", "mov eax, 0x8c"),
        ("c3", "ret"),
        ("b88d000000", "mov eax, 0x8d"),
        ("c3", "ret"),
        ("b88e000000", "mov eax, 0x8e"),
        ("c3", "ret"),
        ("b88f000000", "mov eax, 0x8f"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # A switch at the head of a loop: the cases jump back to the load ('cdqe' here), so there is a
    # label between the bounds check and the load.  Case 0 comes back after a check of its own
    # ('cmp ; jbe' taken), case 2 with a constant index
    "loop_switch": (0x1980, [
        ("4c8d0579e6ffff", "lea r8, [rip - 0x1987]"),
        ("89c8", "mov eax, ecx"),
        ("c1e804", "shr eax, 4"),
        ("83e007", "and eax, 7"),
        ("31d2", "xor edx, edx"),
        ("83f803", "cmp eax, 3"),
        ("7733", "ja 0x19c9"),
        ("4898", "cdqe"),
        ("458b8c8060350000", "mov r9d, dword ptr [r8 + rax*4 + 0x3560]"),
        ("4d01c1", "add r9, r8"),
        ("41ffe1", "jmp r9"),
        ("ffc2", "inc edx"),
        ("8d4201", "lea eax, [rdx + 1]"),
        ("83f803", "cmp eax, 3"),
        ("76e6", "jbe 0x1996"),
        ("eb17", "jmp 0x19c9"),
        ("b815000000", "mov eax, 0x15"),
        ("c3", "ret"),
        ("83c210", "add edx, 0x10"),
        ("b803000000", "mov eax, 3"),
        ("ebd4", "jmp 0x1996"),
        ("8d8200010000", "lea eax, [rdx + 0x100]"),
        ("c3", "ret"),
        ("b8ffffffff", "mov eax, 0xffffffff"),
        ("c3", "ret"),
    ]),
    # ... case 0 comes back with an index nothing has checked: must be skipped
    "loop_nobound": (0x1a00, [
        ("4c8d05f9e5ffff", "lea r8, [rip - 0x1a07]"),
        ("89c8", "mov eax, ecx"),
        ("c1e804", "shr eax, 4"),
        ("83e007", "and eax, 7"),
        ("31d2", "xor edx, edx"),
        ("83f803", "cmp eax, 3"),
        ("772e", "ja 0x1a44"),
        ("4898", "cdqe"),
        ("458b8c8080350000", "mov r9d, dword ptr [r8 + rax*4 + 0x3580]"),
        ("4d01c1", "add r9, r8"),
        ("41ffe1", "jmp r9"),
        ("ffc2", "inc edx"),
        ("8d4201", "lea eax, [rdx + 1]"),
        ("ebe9", "jmp 0x1a16"),
        ("b815000000", "mov eax, 0x15"),
        ("c3", "ret"),
        ("83c210", "add edx, 0x10"),
        ("b803000000", "mov eax, 3"),
        ("ebd9", "jmp 0x1a16"),
        ("8d8200010000", "lea eax, [rdx + 0x100]"),
        ("c3", "ret"),
        ("b8ffffffff", "mov eax, 0xffffffff"),
        ("c3", "ret"),
    ]),
    # ... case 2 can call a function before it comes back, and the base is in a volatile register:
    # the callee (helper: a ret) is closed and does not write it.  (The call is behind a compare the
    # test's inputs never satisfy: the harness's stand-in for a callee overwrites every volatile register.)
    "loop_call": (0x1a80, [
        ("4c8d0579e5ffff", "lea r8, [rip - 0x1a87]"),
        ("89c8", "mov eax, ecx"),
        ("c1e804", "shr eax, 4"),
        ("83e007", "and eax, 7"),
        ("31d2", "xor edx, edx"),
        ("83f803", "cmp eax, 3"),
        ("7740", "ja 0x1ad6"),
        ("4898", "cdqe"),
        ("458b8c80a0350000", "mov r9d, dword ptr [r8 + rax*4 + 0x35a0]"),
        ("4d01c1", "add r9, r8"),
        ("41ffe1", "jmp r9"),
        ("ffc2", "inc edx"),
        ("8d4201", "lea eax, [rdx + 1]"),
        ("83f803", "cmp eax, 3"),
        ("76e6", "jbe 0x1a96"),
        ("eb24", "jmp 0x1ad6"),
        ("b815000000", "mov eax, 0x15"),
        ("c3", "ret"),
        ("83c210", "add edx, 0x10"),
        ("81f945230100", "cmp ecx, 0x12345"),
        ("7505", "jne 0x1ac8"),
        ("e8f8f5ffff", "call 0x10c0"),
        ("b803000000", "mov eax, 3"),
        ("ebc7", "jmp 0x1a96"),
        ("8d8200010000", "lea eax, [rdx + 0x100]"),
        ("c3", "ret"),
        ("b8ffffffff", "mov eax, 0xffffffff"),
        ("c3", "ret"),
    ]),
    # ... the same with a callee that writes the base register (clobber): must be skipped
    "loop_clobber": (0x1b00, [
        ("4c8d05f9e4ffff", "lea r8, [rip - 0x1b07]"),
        ("89c8", "mov eax, ecx"),
        ("c1e804", "shr eax, 4"),
        ("83e007", "and eax, 7"),
        ("31d2", "xor edx, edx"),
        ("83f803", "cmp eax, 3"),
        ("7740", "ja 0x1b56"),
        ("4898", "cdqe"),
        ("458b8c80c0350000", "mov r9d, dword ptr [r8 + rax*4 + 0x35c0]"),
        ("4d01c1", "add r9, r8"),
        ("41ffe1", "jmp r9"),
        ("ffc2", "inc edx"),
        ("8d4201", "lea eax, [rdx + 1]"),
        ("83f803", "cmp eax, 3"),
        ("76e6", "jbe 0x1b16"),
        ("eb24", "jmp 0x1b56"),
        ("b815000000", "mov eax, 0x15"),
        ("c3", "ret"),
        ("83c210", "add edx, 0x10"),
        ("81f945230100", "cmp ecx, 0x12345"),
        ("7505", "jne 0x1b48"),
        ("e898040000", "call 0x1fe0"),
        ("b803000000", "mov eax, 3"),
        ("ebc7", "jmp 0x1b16"),
        ("8d8200010000", "lea eax, [rdx + 0x100]"),
        ("c3", "ret"),
        ("b8ffffffff", "mov eax, 0xffffffff"),
        ("c3", "ret"),
    ]),
    "clobber": (0x1fe0, [
        ("4531c0", "xor r8d, r8d"),
        ("c3", "ret"),
    ]),
    # A jump table whose bound is a constant held in a register ('mov eax, 5 ... cmp edx, eax ; ja'),
    # in a leaf, with the table inside the code, behind the function's last instruction (unit
    # lifter-jt-regbound; the shape of materialsystem_dx11+0x148370).  The cmove between the constant
    # and the compare writes the index, so the bound rules for an immediate find nothing.  Each table
    # is in JUMP_TABLES, six entries.
    # ... a leaf; the bound is in a register that holds a constant; the table lies behind the last ret
    "regbound_switch": (0x1c00, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("39c2", "cmp edx, eax"),
        ("773a", "ja 0x1c4f"),
        ("4863c2", "movsxd rax, edx"),
        ("488d15e1e3ffff", "lea rdx, [rip - 0x1c1f]"),
        ("8b8c82541c0000", "mov ecx, dword ptr [rdx + rax*4 + 0x1c54]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("b833000000", "mov eax, 0x33"),
        ("c3", "ret"),
        ("b834000000", "mov eax, 0x34"),
        ("c3", "ret"),
        ("b835000000", "mov eax, 0x35"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... the same with jae: the constant is the number of entries, not the last index
    "regbound_jae": (0x1f70, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b806000000", "mov eax, 6"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("39c2", "cmp edx, eax"),
        ("7328", "jae 0x1fad"),
        ("4863c2", "movsxd rax, edx"),
        ("488d1571e0ffff", "lea rdx, [rip - 0x1f8f]"),
        ("8b8c82b21f0000", "mov ecx, dword ptr [rdx + rax*4 + 0x1fb2]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... the register is written between the constant and the compare: skipped
    "regbound_written": (0x1c80, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("0f44c1", "cmove eax, ecx"),
        ("39c2", "cmp edx, eax"),
        ("7728", "ja 0x1cc0"),
        ("4863c2", "movsxd rax, edx"),
        ("488d155ee3ffff", "lea rdx, [rip - 0x1ca2]"),
        ("8b8c82c51c0000", "mov ecx, dword ptr [rdx + rax*4 + 0x1cc5]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... a 'lock cmpxchg' stands between the constant and the compare: it writes eax when its compare fails, and the decoder does not say so: skipped (finding 7 of the review of 11 October 2026; unit fix-review-night).  With the open_*
    # programs, in the image's second range of code.
    "regbound_cmpxchg": (0x6800, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("f0410fb108", "lock cmpxchg dword ptr [r8], ecx"),
        ("39c2", "cmp edx, eax"),
        ("7728", "ja 0x6842"),
        ("4863c2", "movsxd rax, edx"),
        ("488d15dc97ffff", "lea rdx, [rip - 0x6824]"),
        ("8b8c8245680000", "mov ecx, dword ptr [rdx + rax*4 + 0x6845]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... an 'xlatb' in between: it writes al, and the decoder does not say so: skipped (finding 7 of the review of 11 October 2026; unit fix-review-night).  With the open_*
    # programs, in the image's second range of code.
    "regbound_xlat": (0x6880, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("d7", "xlatb"),
        ("39c2", "cmp edx, eax"),
        ("7728", "ja 0x68be"),
        ("4863c2", "movsxd rax, edx"),
        ("488d156097ffff", "lea rdx, [rip - 0x68a0]"),
        ("8b8c82c1680000", "mov ecx, dword ptr [rdx + rax*4 + 0x68c1]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... the bound is in ebp and an 'enter' in between writes rbp, which the decoder does not report: skipped (finding 7 of the review of 11 October 2026; unit fix-review-night).  With the open_*
    # programs, in the image's second range of code.
    "regbound_enter": (0x6980, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("bd05000000", "mov ebp, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d5", "cmove edx, ebp"),
        ("c8080000", "enter 8, 0"),
        ("39ea", "cmp edx, ebp"),
        ("7728", "ja 0x69c1"),
        ("4863c2", "movsxd rax, edx"),
        ("488d155d96ffff", "lea rdx, [rip - 0x69a3]"),
        ("8b8c82c4690000", "mov ecx, dword ptr [rdx + rax*4 + 0x69c4]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... an 'xchg r9d, ecx' in between writes neither register of the compare, but it is not an instruction the rule has a checked description of: skipped (finding 7 of the review of 11 October 2026; unit fix-review-night).  With the open_*
    # programs, in the image's second range of code.
    "regbound_unsure": (0x6a00, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("4187c9", "xchg r9d, ecx"),
        ("39c2", "cmp edx, eax"),
        ("7728", "ja 0x6a40"),
        ("4863c2", "movsxd rax, edx"),
        ("488d15de95ffff", "lea rdx, [rip - 0x6a22]"),
        ("8b8c82436a0000", "mov ecx, dword ptr [rdx + rax*4 + 0x6a43]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... the bound register holds a value that is not a constant: skipped
    "regbound_noconst": (0x1d00, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("4489c0", "mov eax, r8d"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("39c2", "cmp edx, eax"),
        ("7728", "ja 0x1d3b"),
        ("4863c2", "movsxd rax, edx"),
        ("488d15e3e2ffff", "lea rdx, [rip - 0x1d1d]"),
        ("8b8c82401d0000", "mov ecx, dword ptr [rdx + rax*4 + 0x1d40]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... one of the six entries is the entry of another function (helper): skipped
    "regbound_outside": (0x1d60, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("39c2", "cmp edx, eax"),
        ("7728", "ja 0x1d9d"),
        ("4863c2", "movsxd rax, edx"),
        ("488d1581e2ffff", "lea rdx, [rip - 0x1d7f]"),
        ("8b8c82a21d0000", "mov ecx, dword ptr [rdx + rax*4 + 0x1da2]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... one of the six entries is a 'mov eax, 0x30 ; ret' of the function after this one: skipped
    "regbound_far": (0x1dc0, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("39c2", "cmp edx, eax"),
        ("7728", "ja 0x1dfd"),
        ("4863c2", "movsxd rax, edx"),
        ("488d1521e2ffff", "lea rdx, [rip - 0x1ddf]"),
        ("8b8c82021e0000", "mov ecx, dword ptr [rdx + rax*4 + 0x1e02]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... a branch arrives between the constant and the compare with another value in the register: skipped
    "regbound_label": (0x1e20, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("85c9", "test ecx, ecx"),
        ("7405", "je 0x1e34"),
        ("b803000000", "mov eax, 3"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("39c2", "cmp edx, eax"),
        ("7728", "ja 0x1e66"),
        ("4863c2", "movsxd rax, edx"),
        ("488d15b8e1ffff", "lea rdx, [rip - 0x1e48]"),
        ("8b8c826b1e0000", "mov ecx, dword ptr [rdx + rax*4 + 0x1e6b]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... a 32-bit compare, but the index is the 64-bit register with no widening copy: skipped
    "regbound_narrow": (0x1e90, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("39c2", "cmp edx, eax"),
        ("7726", "ja 0x1ecb"),
        ("4c8d0554e1ffff", "lea r8, [rip - 0x1eac]"),
        ("418b8c90d01e0000", "mov ecx, dword ptr [r8 + rdx*4 + 0x1ed0]"),
        ("4c01c1", "add rcx, r8"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... one entry points into the middle of an instruction: skipped
    "regbound_inside": (0x1f10, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("39c2", "cmp edx, eax"),
        ("7728", "ja 0x1f4d"),
        ("4863c2", "movsxd rax, edx"),
        ("488d15d1e0ffff", "lea rdx, [rip - 0x1f2f]"),
        ("8b8c82521f0000", "mov ecx, dword ptr [rdx + rax*4 + 0x1f52]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... the index is copied into 16 bits of its register (its other bits come from elsewhere), behind a full copy: skipped
    "regbound_partial": (0x1580, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("39c2", "cmp edx, eax"),
        ("772c", "ja 0x15c1"),
        ("4189d1", "mov r9d, edx"),
        ("664489c8", "mov ax, r9w"),
        ("488d155deaffff", "lea rdx, [rip - 0x15a3]"),
        ("8b8c82c6150000", "mov ecx, dword ptr [rdx + rax*4 + 0x15c6]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
        ("c3", "ret"),
    ]),
    # ... the last arm has no ret of its own and runs into the table, whose first byte is one: skipped
    "regbound_fallin": (0x1b98, [
        ("c1ea04", "shr edx, 4"),
        ("83e207", "and edx, 7"),
        ("b805000000", "mov eax, 5"),
        ("83f9ff", "cmp ecx, -1"),
        ("0f44d0", "cmove edx, eax"),
        ("39c2", "cmp edx, eax"),
        ("7728", "ja 0x1bd5"),
        ("4863c2", "movsxd rax, edx"),
        ("488d1549e4ffff", "lea rdx, [rip - 0x1bb7]"),
        ("8b8c82d71b0000", "mov ecx, dword ptr [rdx + rax*4 + 0x1bd7]"),
        ("4801d1", "add rcx, rdx"),
        ("ffe1", "jmp rcx"),
        ("b830000000", "mov eax, 0x30"),
        ("c3", "ret"),
        ("b831000000", "mov eax, 0x31"),
        ("c3", "ret"),
        ("b832000000", "mov eax, 0x32"),
        ("c3", "ret"),
        ("31c0", "xor eax, eax"),
    ]),
    # A jump table in a leaf that no bounds check of any form ends (unit lifter-leaf-tables; the shape
    # of mileswin64+0xa720, whose caller holds the bound): the index is a 32-bit value extended with
    # movsxd, the table lies behind the code.  Read exactly or stopped loudly: a case for every entry
    # that could be read, a stop with a report for any other address the jump computes.  These are at
    # 0x6000 and up (a second executable range of the image); the index comes in ecx.
    # ... a leaf whose table nothing bounds: five entries, then int3 padding
    "open_switch": (0x6000, [
        ("4c8d05f99fffff", "lea r8, [rip - 0x6007]"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b94802d600000", "mov edx, dword ptr [r8 + rax*4 + 0x602d]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
        ("c3", "ret"),
        ("8d4840", "lea ecx, [rax + 0x40]"),
        ("c3", "ret"),
        ("b933000000", "mov ecx, 0x33"),
        ("c3", "ret"),
    ]),
    # ... the third entry lands in the next function: the reading stops there, two cases
    "open_short": (0x6060, [
        ("4c8d05999fffff", "lea r8, [rip - 0x6067]"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b948083600000", "mov edx, dword ptr [r8 + rax*4 + 0x6083]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
        ("c3", "ret"),
    ]),
    # ... the third entry lands behind the table: the reading stops there, two cases
    "open_beyond": (0x60a0, [
        ("4c8d05599fffff", "lea r8, [rip - 0x60a7]"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b9480c3600000", "mov edx, dword ptr [r8 + rax*4 + 0x60c3]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
        ("c3", "ret"),
    ]),
    # ... the table is in a writable section: skipped
    "open_writable": (0x60e0, [
        ("4c8d05199fffff", "lea r8, [rip - 0x60e7]"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b9480c0300000", "mov edx, dword ptr [r8 + rax*4 + 0x30c0]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
        ("c3", "ret"),
    ]),
    # ... the second entry points into the middle of an instruction: skipped
    "open_inside": (0x6120, [
        ("4c8d05d99effff", "lea r8, [rip - 0x6127]"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b948043610000", "mov edx, dword ptr [r8 + rax*4 + 0x6143]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
        ("c3", "ret"),
    ]),
    # ... the first entry is another function's entry: no entry qualifies, skipped
    "open_none": (0x6160, [
        ("4c8d05999effff", "lea r8, [rip - 0x6167]"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b948083610000", "mov edx, dword ptr [r8 + rax*4 + 0x6183]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
        ("c3", "ret"),
    ]),
    # ... the last arm runs into the table, whose first byte is a ret: skipped
    "open_fallin": (0x61ac, [
        ("4c8d054d9effff", "lea r8, [rip - 0x61b3]"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b9480ce610000", "mov edx, dword ptr [r8 + rax*4 + 0x61ce]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
    ]),
    # ... a case jumps back between the lea of the base and the load: skipped
    "open_label": (0x6200, [
        ("4c8d05f99dffff", "lea r8, [rip - 0x6207]"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b948021620000", "mov edx, dword ptr [r8 + rax*4 + 0x6221]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("31c9", "xor ecx, ecx"),
        ("ebe6", "jmp 0x6207"),
    ]),
    # ... a bounds check that cannot be read (the bound is the caller's, in edx): not 'no bounds check', skipped
    "open_checked": (0x6240, [
        ("4c8d05b99dffff", "lea r8, [rip - 0x6247]"),
        ("39d1", "cmp ecx, edx"),
        ("771c", "ja 0x6267"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b94806a620000", "mov edx, dword ptr [r8 + rax*4 + 0x626a]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
        ("c3", "ret"),
        ("31c9", "xor ecx, ecx"),
        ("c3", "ret"),
    ]),
    # ... the table lies apart from the code and its third entry lands behind the next function's entry: two cases
    "open_far": (0x6290, [
        ("4c8d05699dffff", "lea r8, [rip - 0x6297]"),
        ("4863c1", "movsxd rax, ecx"),
        ("418b9480e0350000", "mov edx, dword ptr [r8 + rax*4 + 0x35e0]"),
        ("4c01c2", "add rdx, r8"),
        ("ffe2", "jmp rdx"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
        ("c3", "ret"),
    ]),
    # ... the function behind open_far; its second half is where that entry points
    "open_far_next": (0x62c0, [
        ("b935000000", "mov ecx, 0x35"),
        ("c3", "ret"),
        ("b936000000", "mov ecx, 0x36"),
        ("c3", "ret"),
    ]),
    # ... the index is a register used whole, which nothing in the function writes (the shape of mileswin64+0x14ab0)
    "open_whole": (0x62e0, [
        ("4c8d0d199dffff", "lea r9, [rip - 0x62e7]"),
        ("418b848906630000", "mov eax, dword ptr [r9 + rcx*4 + 0x6306]"),
        ("4c01c8", "add rax, r9"),
        ("ffe0", "jmp rax"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
        ("c3", "ret"),
        ("b932000000", "mov ecx, 0x32"),
        ("c3", "ret"),
    ]),
    # ... the run starts at a label, the index is made by 32-bit arithmetic and the load overwrites it (the shape of engine+0xfdeb0)
    "open_overwrite": (0x6320, [
        ("39d1", "cmp ecx, edx"),
        ("7703", "ja 0x6327"),
        ("31c9", "xor ecx, ecx"),
        ("c3", "ret"),
        ("89c8", "mov eax, ecx"),
        ("4c8d1dd09cffff", "lea r11, [rip - 0x6330]"),
        ("83e803", "sub eax, 3"),
        ("418b848352630000", "mov eax, dword ptr [r11 + rax*4 + 0x6352]"),
        ("4c01d8", "add rax, r11"),
        ("ffe0", "jmp rax"),
        ("b930000000", "mov ecx, 0x30"),
        ("c3", "ret"),
        ("b931000000", "mov ecx, 0x31"),
        ("c3", "ret"),
        ("b932000000", "mov ecx, 0x32"),
        ("c3", "ret"),
    ]),
    # Jumps through a table of function pointers (unit lifter-tail-tables; discover._tail_through_table): a
    # tail call where the table cannot be a switch table of the function and the stack is shown to be at
    # its entry level.  rt_switch above is the plainest accepted shape.
    # the shape of client+0x25bb90: a frame, the table hangs off the object (a pointer loaded at run
    # time), a 16-bit index, the epilogue, 'jmp rcx'.  rdx is the object so that tests/linked_main.c
    # can hand it one (-DSLOTS_BEHIND)
    "tail_rt": (0x6400, [
        ("53", "push rbx"),
        ("4883ec20", "sub rsp, 0x20"),
        ("4889d3", "mov rbx, rdx"),
        ("6683f9ff", "cmp cx, -1"),
        ("7508", "jne 0x6416"),
        ("31c0", "xor eax, eax"),
        ("4883c420", "add rsp, 0x20"),
        ("5b", "pop rbx"),
        ("c3", "ret"),
        ("488b4308", "mov rax, qword ptr [rbx + 8]"),
        ("0fb7c9", "movzx ecx, cx"),
        ("488b0cc8", "mov rcx, qword ptr [rax + rcx*8]"),
        ("4883c420", "add rsp, 0x20"),
        ("5b", "pop rbx"),
        ("ffe1", "jmp rcx"),
    ]),
    # two functions for tail_rt's table (the really-linked test)
    "tail_a": (0x6430, [
        ("b951000000", "mov ecx, 0x51"),
        ("c3", "ret"),
    ]),
    "tail_b": (0x6438, [
        ("b952000000", "mov ecx, 0x52"),
        ("c3", "ret"),
    ]),
    # two saved registers, a call on the way, the index kept in a callee-saved register, a displacement
    "tail_frame": (0x6440, [
        ("53", "push rbx"),
        ("56", "push rsi"),
        ("4883ec28", "sub rsp, 0x28"),
        ("4889cb", "mov rbx, rcx"),
        ("89d6", "mov esi, edx"),
        ("83e603", "and esi, 3"),
        ("e86dacffff", "call 0x10c0"),
        ("488b4308", "mov rax, qword ptr [rbx + 8]"),
        ("488b44f010", "mov rax, qword ptr [rax + rsi*8 + 0x10]"),
        ("4883c428", "add rsp, 0x28"),
        ("5e", "pop rsi"),
        ("5b", "pop rbx"),
        ("ffe0", "jmp rax"),
    ]),
    # near misses, all skipped.  The epilogue takes 0x20 of the 0x28 off: the stack is 8 below its entry level
    "tail_deep": (0x6470, [
        ("4883ec28", "sub rsp, 0x28"),
        ("488b4108", "mov rax, qword ptr [rcx + 8]"),
        ("488b04d0", "mov rax, qword ptr [rax + rdx*8]"),
        ("4883c420", "add rsp, 0x20"),
        ("ffe0", "jmp rax"),
    ]),
    # the stack is at its entry level, but rbx is not what the caller had
    "tail_unrestored": (0x6490, [
        ("53", "push rbx"),
        ("4889cb", "mov rbx, rcx"),
        ("488b4308", "mov rax, qword ptr [rbx + 8]"),
        ("488b04d0", "mov rax, qword ptr [rax + rdx*8]"),
        ("4883c408", "add rsp, 8"),
        ("ffe0", "jmp rax"),
    ]),
    # both registers popped, each from the other's slot
    "tail_swapped": (0x64b0, [
        ("53", "push rbx"),
        ("56", "push rsi"),
        ("4889cb", "mov rbx, rcx"),
        ("4889d6", "mov rsi, rdx"),
        ("488b4308", "mov rax, qword ptr [rbx + 8]"),
        ("488b04f0", "mov rax, qword ptr [rax + rsi*8]"),
        ("5b", "pop rbx"),
        ("5e", "pop rsi"),
        ("ffe0", "jmp rax"),
    ]),
    # the loaded pointer is changed before the jump
    "tail_modified": (0x64d0, [
        ("488b4108", "mov rax, qword ptr [rcx + 8]"),
        ("488b04d0", "mov rax, qword ptr [rax + rdx*8]"),
        ("4883c004", "add rax, 4"),
        ("ffe0", "jmp rax"),
    ]),
    # one path to the jump pushes, the other does not
    "tail_twolevels": (0x64f0, [
        ("488b4108", "mov rax, qword ptr [rcx + 8]"),
        ("85d2", "test edx, edx"),
        ("7401", "je 0x64f9"),
        ("53", "push rbx"),
        ("488b04d0", "mov rax, qword ptr [rax + rdx*8]"),
        ("ffe0", "jmp rax"),
    ]),
    # the return address is overwritten on the way
    "tail_retslot": (0x6510, [
        ("488b4108", "mov rax, qword ptr [rcx + 8]"),
        ("48891424", "mov qword ptr [rsp], rdx"),
        ("488b04d0", "mov rax, qword ptr [rax + rdx*8]"),
        ("ffe0", "jmp rax"),
    ]),
    # the table's address comes from a read-only slot of the image: it is in the file, so the table
    # could be read, and that is what a table that can be read needs
    "tail_roslot": (0x6530, [
        ("488b05c9cbffff", "mov rax, qword ptr [rip - 0x3437]"),
        ("488b04c8", "mov rax, qword ptr [rax + rcx*8]"),
        ("ffe0", "jmp rax"),
    ]),
    # the base is what the function was entered with in rbx: no argument, nobody's pointer
    "tail_rbxbase": (0x6550, [
        ("488b04d3", "mov rax, qword ptr [rbx + rdx*8]"),
        ("ffe0", "jmp rax"),
    ]),
    # the base is a loaded pointer with something added: computed, not loaded
    "tail_computed": (0x6560, [
        ("488b4108", "mov rax, qword ptr [rcx + 8]"),
        ("4883c010", "add rax, 0x10"),
        ("488b04d0", "mov rax, qword ptr [rax + rdx*8]"),
        ("ffe0", "jmp rax"),
    ]),
    # the shape of engine+0x2526d0: a leaf that never touches the stack, a read-only table of function
    # pointers with empty slots and no bound on the index (JUMP_PTR_TABLES)
    "tail_ro": (0x6580, [
        ("4863c2", "movsxd rax, edx"),
        ("488d1576d0ffff", "lea rdx, [rip - 0x2f8a]"),
        ("488b14c2", "mov rdx, qword ptr [rdx + rax*8]"),
        ("4885d2", "test rdx, rdx"),
        ("7402", "je 0x6595"),
        ("ffe2", "jmp rdx"),
        ("c3", "ret"),
    ]),
    # ... one slot of the table holds a label of this function (its ret): a switch table, or half of
    # one, which must be read with a bound or not at all: skipped
    "tail_ro_label": (0x65a0, [
        ("4863c2", "movsxd rax, edx"),
        ("488d1596d0ffff", "lea rdx, [rip - 0x2f6a]"),
        ("488b14c2", "mov rdx, qword ptr [rdx + rax*8]"),
        ("4885d2", "test rdx, rdx"),
        ("7402", "je 0x65b5"),
        ("ffe2", "jmp rdx"),
        ("c3", "ret"),
    ]),
    # ... the table holds no function at all, only empty slots: nothing shows what it is: skipped
    "tail_ro_none": (0x65c0, [
        ("4863c2", "movsxd rax, edx"),
        ("488d1596d0ffff", "lea rdx, [rip - 0x2f6a]"),
        ("488b14c2", "mov rdx, qword ptr [rdx + rax*8]"),
        ("4885d2", "test rdx, rdx"),
        ("7402", "je 0x65d5"),
        ("ffe2", "jmp rdx"),
        ("c3", "ret"),
    ]),
    # the shape of engine+0xf130: a table of 16-byte records in writable data, the epilogue, then
    # 'jmp [table + index*8 + 8]'
    "tail_wr": (0x65e0, [
        ("53", "push rbx"),
        ("4883ec40", "sub rsp, 0x40"),
        ("4889cb", "mov rbx, rcx"),
        ("4863431c", "movsxd rax, dword ptr [rbx + 0x1c]"),
        ("488d154dcaffff", "lea rdx, [rip - 0x35b3]"),
        ("4801c0", "add rax, rax"),
        ("4889d9", "mov rcx, rbx"),
        ("4883c440", "add rsp, 0x40"),
        ("5b", "pop rbx"),
        ("ff64c208", "jmp qword ptr [rdx + rax*8 + 8]"),
    ]),
    # ... the writable table holds, as the file has it, a label of this very function (the mov behind
    # the jump): a switch whose table happens to be writable.  It cannot be read, so: skipped
    "tail_wr_label": (0x6620, [
        ("53", "push rbx"),
        ("89cb", "mov ebx, ecx"),
        ("83e301", "and ebx, 1"),
        ("488d15e3c9ffff", "lea rdx, [rip - 0x361d]"),
        ("488b04da", "mov rax, qword ptr [rdx + rbx*8]"),
        ("5b", "pop rbx"),
        ("ffe0", "jmp rax"),
        ("b807000000", "mov eax, 7"),
        ("c3", "ret"),
    ]),
    # the shape of engine+0x5c0b0 and +0x5c100: records in writable data, the index shifted beforehand,
    # so the load has no scale and the table's address is the load's second register
    "tail_shift": (0x6640, [
        ("53", "push rbx"),
        ("4883ec20", "sub rsp, 0x20"),
        ("4889d3", "mov rbx, rdx"),
        ("83f903", "cmp ecx, 3"),
        ("7722", "ja 0x666f"),
        ("4863c1", "movsxd rax, ecx"),
        ("4c8d1539caffff", "lea r10, [rip - 0x35c7]"),
        ("48c1e004", "shl rax, 4"),
        ("4e8b1410", "mov r10, qword ptr [rax + r10]"),
        ("4d85d2", "test r10, r10"),
        ("740b", "je 0x666f"),
        ("4889d9", "mov rcx, rbx"),
        ("4883c420", "add rsp, 0x20"),
        ("5b", "pop rbx"),
        ("41ffe2", "jmp r10"),
        ("31c0", "xor eax, eax"),
        ("4883c420", "add rsp, 0x20"),
        ("5b", "pop rbx"),
        ("c3", "ret"),
    ]),
    # ... the same load from a read-only table: an address the file fixes, and without a scale no
    # way to tell the slots apart.  Skipped
    "tail_shift_ro": (0x6680, [
        ("4863c1", "movsxd rax, ecx"),
        ("4c8d15f6cfffff", "lea r10, [rip - 0x300a]"),
        ("48c1e004", "shl rax, 4"),
        ("4e8b1410", "mov r10, qword ptr [rax + r10]"),
        ("41ffe2", "jmp r10"),
    ]),
    # both registers of an unscaled load are addresses the file fixes: skipped
    "tail_two_leas": (0x66a0, [
        ("488d0599c9ffff", "lea rax, [rip - 0x3667]"),
        ("488d0de2c9ffff", "lea rcx, [rip - 0x361e]"),
        ("488b0408", "mov rax, qword ptr [rax + rcx]"),
        ("ffe0", "jmp rax"),
    ]),
    # the shape of vphysics+0x11c5b0 (and of client+0x282e70's frame): rsi saved in the home area
    # before the push, rbx saved there later and on one path only, a call, the table in writable
    # data, both registers loaded back from the home area, then the epilogue
    "tail_home": (0x66e0, [
        ("4889742410", "mov qword ptr [rsp + 0x10], rsi"),
        ("57", "push rdi"),
        ("4883ec20", "sub rsp, 0x20"),
        ("48637210", "movsxd rsi, dword ptr [rdx + 0x10]"),
        ("4889d7", "mov rdi, rdx"),
        ("83fe03", "cmp esi, 3"),
        ("7729", "ja 0x671f"),
        ("48895c2430", "mov qword ptr [rsp + 0x30], rbx"),
        ("e8c0a9ffff", "call 0x10c0"),
        ("4c8d0539c9ffff", "lea r8, [rip - 0x36c7]"),
        ("4889f9", "mov rcx, rdi"),
        ("498b04f0", "mov rax, qword ptr [r8 + rsi*8]"),
        ("488b5c2430", "mov rbx, qword ptr [rsp + 0x30]"),
        ("488b742438", "mov rsi, qword ptr [rsp + 0x38]"),
        ("4883c420", "add rsp, 0x20"),
        ("5f", "pop rdi"),
        ("ffe0", "jmp rax"),
        ("488b742438", "mov rsi, qword ptr [rsp + 0x38]"),
        ("4883c420", "add rsp, 0x20"),
        ("5f", "pop rdi"),
        ("c3", "ret"),
    ]),
    # near misses of the home-area saves, all skipped.  rsi is loaded back from rbx's slot
    "tail_home_other": (0x6740, [
        ("48895c2408", "mov qword ptr [rsp + 8], rbx"),
        ("4889742410", "mov qword ptr [rsp + 0x10], rsi"),
        ("4889d6", "mov rsi, rdx"),
        ("488b4108", "mov rax, qword ptr [rcx + 8]"),
        ("488b04f0", "mov rax, qword ptr [rax + rsi*8]"),
        ("488b742408", "mov rsi, qword ptr [rsp + 8]"),
        ("ffe0", "jmp rax"),
    ]),
    # rsi's slot is overwritten between the save and the load back
    "tail_home_lost": (0x6760, [
        ("4889742410", "mov qword ptr [rsp + 0x10], rsi"),
        ("4889d6", "mov rsi, rdx"),
        ("4889542410", "mov qword ptr [rsp + 0x10], rdx"),
        ("488b4108", "mov rax, qword ptr [rcx + 8]"),
        ("488b04f0", "mov rax, qword ptr [rax + rsi*8]"),
        ("488b742410", "mov rsi, qword ptr [rsp + 0x10]"),
        ("ffe0", "jmp rax"),
    ]),
    # rsi is saved after it was changed: the slot does not hold what the caller had
    "tail_home_late": (0x6780, [
        ("4889d6", "mov rsi, rdx"),
        ("4889742410", "mov qword ptr [rsp + 0x10], rsi"),
        ("488b4108", "mov rax, qword ptr [rcx + 8]"),
        ("488b04f0", "mov rax, qword ptr [rax + rsi*8]"),
        ("488b742410", "mov rsi, qword ptr [rsp + 0x10]"),
        ("ffe0", "jmp rax"),
    ]),
    # the shape of client+0x306540: the pointer is a member of the caller's object at an offset that is
    # read from memory; the load has no scale, and both its registers are values got at run time
    "tail_member": (0x67a0, [
        ("53", "push rbx"),
        ("4883ec20", "sub rsp, 0x20"),
        ("448b1a", "mov r11d, dword ptr [rdx]"),
        ("4889cb", "mov rbx, rcx"),
        ("498b041b", "mov rax, qword ptr [r11 + rbx]"),
        ("4883c420", "add rsp, 0x20"),
        ("5b", "pop rbx"),
        ("ffe0", "jmp rax"),
    ]),
    # ... the offset is computed after it was read: skipped (nothing shows what was added)
    "tail_member_sum": (0x67c0, [
        ("448b1a", "mov r11d, dword ptr [rdx]"),
        ("4983c308", "add r11, 8"),
        ("498b040b", "mov rax, qword ptr [r11 + rcx]"),
        ("ffe0", "jmp rax"),
    ]),
}
JUMP_LABELS = {n: rva for n, (rva, _) in JUMP_PROGRAMS.items()}
# table rva -> (what entries are relative to: None = image base, i.e. RVAs; else that rva,
#               the case addresses, entries that follow the table)
JUMP_TABLES = {
    0x3200: (0x3200, [0x1117, 0x111d, 0x1123, 0x1129], [0x1f00, 0x1f00]),          # clang_switch
    0x3220: (0x3220, [0x1196, 0x119c, 0x11a2, 0x11a8], [0x1f00, 0x1f00]),          # and_switch
    0x3240: (None, [0x122a, 0x1230, 0x1236, 0x123c, 0x1242, 0x1248], []),          # two_checks
    0x3260: (0x3260, [0x129a, 0x12a0, 0x12a6, 0x12ac], [0x1f00, 0x1f00]),          # hoisted_switch
    0x3280: (0x3280, [0x1323, 0x1329], []),                                        # ambig_switch, one base
    0x3290: (0x3290, [0x1329, 0x1323], []),                                        # ambig_switch, the other
    0x3400: (0x3400, [0x1562, 0x1568, 0x156e, 0x1574], [0x1f00, 0x1f00]),          # two_lea_switch
    0x3500: (None, [0x1827, 0x182d, 0x1833, 0x1839], []), # two_reg_switch
    0x3520: (None, [0x18a3, 0x18a9, 0x18af, 0x18b5], []), # two_reg_bad
    0x3540: (None, [0x1922, 0x1928, 0x192e, 0x1934], []), # swapped_switch
    0x3560: (None, [0x19a6, 0x19b2, 0x19b8, 0x19c2], []), # loop_switch
    0x3580: (None, [0x1a26, 0x1a2d, 0x1a33, 0x1a3d], []), # loop_nobound
    0x35a0: (None, [0x1aa6, 0x1ab2, 0x1ab8, 0x1acf], []), # loop_call
    0x35c0: (None, [0x1b26, 0x1b32, 0x1b38, 0x1b4f], []), # loop_clobber
    # tables inside the code, each behind its function (regbound_*)
    0x1c54: (None, [0x1c2b, 0x1c31, 0x1c37, 0x1c3d, 0x1c43, 0x1c49], []), # regbound_switch
    0x1fb2: (None, [0x1f9b, 0x1f9b, 0x1fa1, 0x1fa7, 0x1fa7, 0x1fa7], []), # regbound_jae
    0x1cc5: (None, [0x1cae, 0x1cae, 0x1cb4, 0x1cba, 0x1cba, 0x1cba], []), # regbound_written
    0x1d40: (None, [0x1d29, 0x1d29, 0x1d2f, 0x1d35, 0x1d35, 0x1d35], []), # regbound_noconst
    0x1da2: (None, [0x1d8b, 0x1d8b, 0x1d91, 0x1d97, 0x10c0, 0x1d97], []), # regbound_outside
    0x1e02: (None, [0x1deb, 0x1deb, 0x1df1, 0x1df7, 0x1e54, 0x1df7], []), # regbound_far (0x1e54: a case of regbound_label)
    0x1e6b: (None, [0x1e54, 0x1e54, 0x1e5a, 0x1e60, 0x1e60, 0x1e60], []), # regbound_label
    0x1ed0: (None, [0x1eb9, 0x1eb9, 0x1ebf, 0x1ec5, 0x1ec5, 0x1ec5], []), # regbound_narrow
    0x1f52: (None, [0x1f3b, 0x1f3c, 0x1f41, 0x1f47, 0x1f47, 0x1f47], []), # regbound_inside
    0x1bd7: (None, [0x1bc3, 0x1bc3, 0x1bc9, 0x1bcf, 0x1bcf, 0x1bcf], []), # regbound_fallin
    0x15c6: (None, [0x15af, 0x15af, 0x15b5, 0x15bb, 0x15bb, 0x15bb], []), # regbound_partial
    0x6845: (None, [0x6830, 0x6830, 0x6836, 0x683c, 0x683c, 0x683c], []), # regbound_cmpxchg
    0x68c1: (None, [0x68ac, 0x68ac, 0x68b2, 0x68b8, 0x68b8, 0x68b8], []), # regbound_xlat
    0x69c4: (None, [0x69af, 0x69af, 0x69b5, 0x69bb, 0x69bb, 0x69bb], []), # regbound_enter
    0x6a43: (None, [0x6a2e, 0x6a2e, 0x6a34, 0x6a3a, 0x6a3a, 0x6a3a], []), # regbound_unsure
    # tables that nothing bounds (open_*); two int3 words follow the first, as padding does in the game
    0x602d: (None, [0x6017, 0x601d, 0x6023, 0x601d, 0x6027], [-0x33333334, -0x33333334]), # open_switch
    0x6083: (None, [0x6077, 0x607d, 0x60bd, 0x607d], []), # open_short
    0x60c3: (None, [0x60b7, 0x60bd, 0x60d4], []), # open_beyond
    0x30c0: (None, [0x60f7, 0x60fd], []), # open_writable
    0x6143: (None, [0x6137, 0x6138, 0x613d], []), # open_inside
    0x6183: (None, [0x6000, 0x6177], []), # open_none
    0x61ce: (None, [0x61c3, 0x61c3, 0x61c9], []), # open_fallin
    0x6221: (None, [0x6217, 0x621d], []), # open_label
    0x626a: (None, [0x625b, 0x6261], []), # open_checked
    0x35e0: (None, [0x62a7, 0x62ad, 0x62c6], []), # open_far (the table is not behind its code)
    0x6306: (None, [0x62f4, 0x62fa, 0x62f4, 0x6300], [-0x33333334]), # open_whole
    0x6352: (None, [0x6340, 0x6346, 0x634c], []), # open_overwrite
}
# Bytes that no entry names: a ret behind open_beyond's table, where its third entry points.
JUMP_BYTES = {0x60d4: "c3"}
# table rva -> (the code addresses its 8-byte slots hold, the slots that have a relocation entry).
# 0x3080 is in the image's writable range.
JUMP_ABS_TABLES = {
    0x3300: ([0x1430, 0x1436, 0x143c, 0x1442], (0, 1, 2, 3)),                      # goto_fn
    0x3340: ([0x10c0, 0x1060], (0, 1)),                                            # fnptr_tail: helper, slot_jmp
    0x3080: ([0x148d, 0x1493], (0, 1)),                                            # wr_switch
    0x3380: ([0x14cd, 0x14d3], (0,)),                                              # norel_switch
    0x33c0: ([0x150c, 0x1512], (0, 1)),                                            # unb_switch
}

# Tables of function pointers in read-only data, for the tail_ro* programs: table rva -> the 8-byte
# slots, each the rva of the code it points at (a relocated pointer) or None (zero, no relocation).
# Eight bytes that are neither follow each table, as other data follows such a table in the game.
JUMP_PTR_TABLES = {
    0x3600: [None, 0x6430, None, 0x6438, None],                                    # tail_ro: tail_a, tail_b
    0x3640: [0x6430, 0x65b5],                                                      # tail_ro_label: tail_a, its own ret
    0x3660: [None, None],                                                          # tail_ro_none
    # in the image's writable range (0x3000 to 0x3100):
    0x3040: [None, 0x6430, None, 0x6438],                                          # tail_wr: 16-byte records, tail_a, tail_b
    0x3010: [0x6632],                                                              # tail_wr_label: the mov behind its jump
    0x3090: [0x6430, None, 0x6438, None],                                          # tail_shift: 16-byte records
    0x3680: [0x6430, None],                                                        # tail_shift_ro (read-only)
}

# Two functions that tail-call each other ecx times: for the really-linked build, where a
# guest tail call must not use host stack (run_selftests.test_linked_tail_calls).  ping and
# pong jump directly; iping and ipong through a register and through a slot (issue 49), with
# rdx pointing at the two slots {iping, ipong}.
TAIL_PROGRAMS = {
    "iping": (0x1040, [
        ("ffc9", "dec ecx"),
        ("7406", "je 0x104a"),
        ("488b4208", "mov rax, qword ptr [rdx + 8]"),
        ("ffe0", "jmp rax"),
        ("c3", "ret"),
    ]),
    "ipong": (0x1060, [
        ("ffc9", "dec ecx"),
        ("7402", "je 0x1066"),
        ("ff22", "jmp qword ptr [rdx]"),
        ("c3", "ret"),
    ]),
    "ping": (0x1000, [
        ("ffc9", "dec ecx"),
        ("7405", "je 0x1009"),
        ("e917000000", "jmp 0x1020"),
        ("c3", "ret"),
    ]),
    "pong": (0x1020, [
        ("ffc9", "dec ecx"),
        ("7405", "je 0x1029"),
        ("e9d7ffffff", "jmp 0x1000"),
        ("c3", "ret"),
    ]),
}
# Calls whose callee does or does not come back the way it was called, for the return check of
# the really-linked build (run_selftests.test_linked_return_check, issue 49).  An image of
# their own.  slot_caller calls through [rdx].
RET_PROGRAMS = {
    "ok_caller": (0x1000, [
        ("4883ec28", "sub rsp, 0x28"),
        ("e817000000", "call 0x1020"),
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
    "ok_leaf": (0x1020, [
        ("83c164", "add ecx, 0x64"),
        ("c3", "ret"),
    ]),
    "ret8_caller": (0x1040, [
        ("4883ec28", "sub rsp, 0x28"),
        ("e817000000", "call 0x1060"),
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
    "ret8": (0x1060, [                                    # pops 8 bytes its caller does not expect
        ("c20800", "ret 8"),
    ]),
    "smash_caller": (0x1080, [
        ("4883ec28", "sub rsp, 0x28"),
        ("e817000000", "call 0x10a0"),
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
    "smash": (0x10a0, [                                   # returns to rcx: rsp ends up right
        ("48890c24", "mov qword ptr [rsp], rcx"),
        ("c3", "ret"),
    ]),
    "pushret_caller": (0x10c0, [
        ("4883ec28", "sub rsp, 0x28"),
        ("e817000000", "call 0x10e0"),
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
    "pushret": (0x10e0, [                                 # ret used as a jump to rcx
        ("51", "push rcx"),
        ("c3", "ret"),
    ]),
    "slot_caller": (0x1100, [
        ("4883ec28", "sub rsp, 0x28"),
        ("ff12", "call qword ptr [rdx]"),
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
}
# Addresses taken by a RIP-relative lea (issue 51); an image of their own
# (run_selftests.test_lea_discovery).  lea_caller is the only given entry besides lea_known;
# lea_unwound has an unwind entry.  LEA_DATA is what else sits in the executable section.
LEA_PROGRAMS = {
    "lea_caller": (0x1000, [
        ("4883ec28", "sub rsp, 0x28"),
        ("488d0535000000", "lea rax, [rip + 0x35]"),      # lea_leaf: a function nothing else names
        ("ffd0", "call rax"),
        ("488d154c000000", "lea rdx, [rip + 0x4c]"),      # 0x1060: bytes that do not decode
        ("488d154d000000", "lea rdx, [rip + 0x4d]"),      # 0x1068: a string
        ("4c8d054e000000", "lea r8, [rip + 0x4e]"),       # 0x1070: int3 padding
        ("4c8d0d5b000000", "lea r9, [rip + 0x5b]"),       # 0x1084: a label inside lea_unwound
        ("4c8d1590000000", "lea r10, [rip + 0x90]"),      # lea_known: a function already
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
    "lea_leaf": (0x1040, [
        ("83c164", "add ecx, 0x64"),
        ("c3", "ret"),
    ]),
    "lea_unwound": (0x1080, [
        ("31c0", "xor eax, eax"),
        ("85c9", "test ecx, ecx"),
        ("0f94c0", "sete al"),
        ("c3", "ret"),
    ]),
    "lea_known": (0x10c0, [
        ("c3", "ret"),
    ]),
}
LEA_LABELS = {n: rva for n, (rva, _) in LEA_PROGRAMS.items()}
LEA_DATA = {0x1060: b"\xff" * 8, 0x1068: b"linear\0", 0x1070: b"\xcc" * 4}

LABELS = {"sum_loop": 0x1000, "calls_out": 0x1040, "helper": 0x1080, "cond_tail": 0x10c0, "reg_writes": 0x1100, "stack_ops": 0x1180, "flags_mix": 0x11c0, "mem_ops": 0x1240, "sse_moves": 0x12c0, "string_ops": 0x1380, "split_main": 0x1440, "split_back": 0x1449, "split_frag": 0x1480, "switch_fn": 0x14c0, "sw_case0": 0x14df, "sw_case1": 0x14e5, "sw_case2": 0x14eb, "sw_case3": 0x14f1, "sw_default": 0x14f7, "noret_fn": 0x1540, "fall_a": 0x1580, "fall_b": 0x1582, "div_fn": 0x15c0, "args_fn": 0x1600, "twocalls_fn": 0x1640, "div_trap_fn": 0x1680, "trap_fn": 0x16a0, "cov_fn": 0x16c0, "fourargs_fn": 0x1700, "gptr_fn": 0x1740, "memint_fn": 0x1780, "jmp_rsp_fn": 0x17c0, "far_read_fn": 0x1800, "far_write_fn": 0x1840, "imp_data_fn": 0x1880, "noncanon_fn": 0x18c0, "page_walk_fn": 0x1900, "call_loop_fn": 0x1940, "count_loop_fn": 0x1980, "spin_fn": 0x19c0, "probe_fn": 0x1a00, "probe_stub_fn": 0x1a40, "probe_ok_fn": 0x1a60, "data0": 0x3000, "data1": 0x3008, "data2": 0x3010, "ptr0": 0x3018, "gptr": 0x3020, "imp0": 0x4000, "imp1": 0x4008}

# ---- single-instruction vectors, one file per instruction family (see the top of this file) -------

FAMILY_VECTORS = {}      # family -> [(rva, hex, text)]
FAMILY_REFUSED = {}      # family -> [(rva, hex, text, why)]
for _m in sorted(pkgutil.iter_modules(__path__), key=lambda m: m.name):
    if not _m.name.startswith("_"):
        _mod = importlib.import_module("%s.%s" % (__name__, _m.name))
        FAMILY_VECTORS[_m.name] = list(_mod.VECTORS)
        FAMILY_REFUSED[_m.name] = list(getattr(_mod, "REFUSED", ()))

# Instructions the oracle executes wrongly (review finding 8): translated, but every case
# must come out inconclusive, never passing or failing.
ORACLE_BUG_VECTORS = [
    (0x3ec0, "480fac054341000004", "shrd qword ptr [rip + 0x4143], rax, 4"),
    (0x3ed0, "480fa4054341000004", "shld qword ptr [rip + 0x4143], rax, 4"),
    (0x3ef0, "0fac054341000004", "shrd dword ptr [rip + 0x4143], eax, 4"),
]

# Encodings the lifter must refuse (the function containing one is skipped and reported):
# (rva, hex, Capstone's text, a substring of the reason the lifter gives).
UNSUPPORTED_VECTORS = [
    # review finding 11: nothing can check the flags of 'lock neg'
    (0x4000, "f0f71b", "lock neg dword ptr [rbx]", "lock neg"),
    (0x4010, "f048f75b08", "lock neg qword ptr [rbx + 8]", "lock neg"),
    (0x4020, "66f0f71b", "lock neg word ptr [rbx]", "lock neg"),
    (0x4030, "f0f61b", "lock neg byte ptr [rbx]", "lock neg"),
    # review finding 7: far call/jmp through memory, which Capstone names 'call'/'jmp' without REX.W
    (0x40a0, "ff1b", "call ptr [rbx]", "call (opcode ff /3"),
    (0x40b0, "ff2b", "jmp ptr [rbx]", "jmp (opcode ff /5"),
    (0x40c0, "48ff1b", "lcall [rbx]", "lcall"),
    (0x40d0, "48ff2b", "ljmp [rbx]", "ljmp"),
    # other things a translator registered under the same name was not written for
    (0x40e0, "48f3a5", "rep movsd dword ptr [rdi], dword ptr [rsi]", "does not parse"),   # REX before a prefix
    (0x40f0, "8cd8", "mov eax, ds", "mov (opcode 8c"),
    (0x4100, "0fefc1", "pxor mm0, mm1", "pxor (opcode fef"),
    (0x4110, "0f6fc1", "movq mm0, mm1", "movq (opcode f6f"),
    (0x4120, "c5f828c1", "vmovaps xmm0, xmm1", "vmovaps"),
    # review finding 6: prefix bytes that Capstone does not report.  'f2 a5' repeats rcx times
    # on a real CPU; the 'f0 f3' / 'f0 f2' forms are locked.
    (0x4130, "f2a5", "movsd dword ptr [rdi], dword ptr [rsi]", "F2/F3 prefix that the decoder does not report"),
    (0x4140, "f3f2a5", "movsd dword ptr [rdi], dword ptr [rsi]", "both F2 and F3"),
    (0x4150, "f2f3a5", "rep movsd dword ptr [rdi], dword ptr [rsi]", "both F2 and F3"),
    (0x4160, "f0f3ff03", "inc dword ptr [rbx]", "LOCK prefix that the decoder does not report"),
    (0x4170, "f0f20fb10b", "cmpxchg dword ptr [rbx], ecx", "LOCK prefix that the decoder does not report"),
    (0x4180, "f3f0ff03", "xrelease lock inc dword ptr [rbx]", "LOCK together with an F2/F3 prefix"),
    (0x4190, "f2f00fb10b", "xacquire lock cmpxchg dword ptr [rbx], ecx", "LOCK together with an F2/F3 prefix"),
    # review finding 13: near branches with an operand-size prefix (Intel and AMD differ)
    (0x41a0, "66ffe2", "jmp rdx", "jmp with a 0x66 prefix"),
    (0x41b0, "66ffd2", "call rdx", "call with a 0x66 prefix"),
    (0x41c0, "66e800000000", "call 0x41c6", "call with a 0x66 prefix"),
    (0x41d0, "66e900000000", "jmp 0x41d6", "jmp with a 0x66 prefix"),
    (0x41e0, "66c3", "ret", "ret with a 0x66 prefix"),
    (0x41f0, "660f8400000000", "je 0x41f7", "je with a 0x66 prefix"),
    (0x4200, "6675fe", "jne 0x4201", "jne with a 0x66 prefix"),
    (0x4210, "66c20800", "ret 8", "ret with a 0x66 prefix"),
    # An instruction whose bytes are partly a relocated pointer that begins before it (UNSUPPORTED_RELOCS):
    # loading the image changes them (here the immediate becomes 0x002c), so it must be refused.
    (0x4220, "c22c80", "ret 0x802c", "relocation that begins before ret"),
]
# Relocation slots (8 bytes each) in the image of UNSUPPORTED_VECTORS.
UNSUPPORTED_RELOCS = [0x421d]

# Name clashes (issue 6): Capstone gives an SSE compare with a predicate the id name of another
# instruction ('cmpsq', 'cmpsb', 'cmppd').  The lifter names an instruction by the mnemonic the
# decoder prints, so each of these is either translated by the SSE compare family that registered
# that mnemonic (sse_cmpss, sse_cmpsd, sse_cmpps) or, where no family has it, refused under its
# real name; it never reaches the translator of the instruction whose id name it carries.
# These were entries of UNSUPPORTED_VECTORS until the compare families existed: a list of
# encodings that "must be refused" cannot hold an instruction a family may translate.
# (rva, hex, Capstone's text, Capstone's id name), in the image of UNSUPPORTED_VECTORS.
NAME_CLASH_VECTORS = [
    (0x4040, "f20fc2c104", "cmpneqsd xmm0, xmm1", "cmpsq"),
    (0x4050, "0fc2c102", "cmpleps xmm0, xmm1", "cmpsb"),
    (0x4060, "f30fc2c104", "cmpneqss xmm0, xmm1", "cmpsq"),
    (0x4070, "0fc2c104", "cmpneqps xmm0, xmm1", "cmpsq"),
    (0x4080, "f20fc20d0000000004", "cmpneqsd xmm1, qword ptr [rip]", "cmpsq"),
    (0x4090, "f20fc2c100", "cmpeqsd xmm0, xmm1", "cmppd"),
]
