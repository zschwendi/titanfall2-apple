/* Hand-written versions of three lifted functions with the guest registers in C locals, for
 * the gap analysis (docs/findings/25-lifted-speed.md, "Registers living in memory").
 *
 * Not compiled on its own: bench/variants.py (transformation hand:locals) substitutes each
 * function below for the lifter's version in a copy of the module's C, if the lifter's text
 * still has the SHA-256 named in the comment above the function.
 *
 * What a lifter that kept registers in locals would emit, done by hand with one fixed rule
 * so that it is not tuned per function:
 *   - every guest register the function touches is loaded from the Ctx into a local at entry;
 *   - every register the function writes is stored back before each return, and every
 *     register it touches is stored before a call and loaded again after it (the callee is
 *     lifted code that reads and writes the Ctx);
 *   - a status flag is computed only where an instruction reads it, as a C comparison at the
 *     branch; no flag is stored in the Ctx (they are dead at every return and call);
 *   - guest memory accesses, pushes, pops, the call macro and the return macro are the
 *     lifter's.
 * One statement per x86 instruction, in the same order, with the instruction as a comment.
 */

/* str_cmp (mem_str.c) */
/* replaces one_mem_str_1590, lifter text sha256 92ac15183901c3986c4398d19cfe609b9b82b55a28de0b80804302b0fcf5fdd7 */
void one_mem_str_1590(Ctx *ctx) {
    uint64_t rax = ctx->r[0], rcx = ctx->r[1], rdx = ctx->r[2], r8 = ctx->r[8], r9 = ctx->r[9], r10 = ctx->r[10];
    uint8_t a8, b8;
    /* 1590: test r8, r8 ; 1593: je 0x15bc */
    if (r8 == 0) goto L_15bc;
    /* 1595: dec r8 */
    r8 = r8 - 1;
    /* 1598: xor r9d, r9d */
    r9 = 0;
L_15a0: ;
    /* 15a0: movzx r10d, byte ptr [rcx + r9] */
    r10 = (uint32_t)LD8(rcx + r9);
    /* 15a5: cmp r10b, byte ptr [rdx + r9] ; 15a9: jne 0x15bf */
    a8 = (uint8_t)r10; b8 = LD8(rdx + r9);
    if (a8 != b8) goto L_15bf;
    /* 15ab: xor eax, eax */
    rax = 0;
    /* 15ad: test r10b, r10b ; 15b0: je 0x15bb */
    if ((uint8_t)r10 == 0) goto L_15bb;
    /* 15b2: cmp r8, r9 ; 15b5: lea r9, [r9 + 1] ; 15b9: jne 0x15a0 */
    { int ne = r8 != r9; r9 = r9 + 1; if (ne) goto L_15a0; }
L_15bb: ;
    /* 15bb: ret  */
    ctx->r[0] = rax; ctx->r[8] = r8; ctx->r[9] = r9; ctx->r[10] = r10;
    { RET(0); }
L_15bc: ;
    /* 15bc: xor eax, eax */
    rax = 0;
    /* 15be: ret  */
    ctx->r[0] = rax; ctx->r[8] = r8; ctx->r[9] = r9; ctx->r[10] = r10;
    { RET(0); }
L_15bf: ;
    /* 15bf: setae al */
    rax = (rax & ~UINT64_C(0xFF)) | (uint64_t)(a8 >= b8);
    /* 15c2: movzx eax, al */
    rax = (uint32_t)(uint8_t)rax;
    /* 15c5: lea eax, [rax*2 - 1] */
    rax = (uint32_t)(rax * 2 + UINT64_C(0xffffffffffffffff));
    /* 15cc: ret  */
    ctx->r[0] = rax; ctx->r[8] = r8; ctx->r[9] = r9; ctx->r[10] = r10;
    { RET(0); }
}

/* fib (calls.c) */
/* replaces one_calls_1000, lifter text sha256 98da72712b6d3b19a2c5bd5ddd088bc343299baf7d30f0d93904b02c3913f2b7 */
void one_calls_1000(Ctx *ctx) {
    uint64_t rax = ctx->r[0], rcx = ctx->r[1], rsp = ctx->r[4], rsi = ctx->r[6], rdi = ctx->r[7];
    /* 1000: push rsi */
    rsp -= 8; ST64(rsp, rsi);
    /* 1001: push rdi */
    rsp -= 8; ST64(rsp, rdi);
    /* 1002: sub rsp, 0x28 */
    rsp -= UINT64_C(0x28);
    /* 1006: mov esi, ecx */
    rsi = (uint32_t)rcx;
    /* 1008: and esi, 0xf */
    rsi = (uint32_t)rsi & UINT32_C(0xf);
    /* 100b: xor edi, edi */
    rdi = 0;
    /* 100d: cmp esi, 2 ; 1010: jb 0x1035 */
    if ((uint32_t)rsi < UINT32_C(0x2)) goto L_1035;
    /* 1012: xor edi, edi */
    rdi = 0;
L_1020: ;
    /* 1020: lea ecx, [rsi - 1] */
    rcx = (uint32_t)(rsi + UINT64_C(0xffffffffffffffff));
    /* 1023: call 0x1000 */
    ctx->r[0] = rax; ctx->r[1] = rcx; ctx->r[4] = rsp; ctx->r[6] = rsi; ctx->r[7] = rdi;
    { CALL_DIRECT(one_calls_1000, 0x1000, 0x1028); }
    rax = ctx->r[0]; rcx = ctx->r[1]; rsp = ctx->r[4]; rsi = ctx->r[6]; rdi = ctx->r[7];
    /* 1028: add esi, 0xe */
    rsi = (uint32_t)((uint32_t)rsi + UINT32_C(0xe));
    /* 102b: add edi, eax */
    rdi = (uint32_t)((uint32_t)rdi + (uint32_t)rax);
    /* 102d: and esi, 0xf */
    rsi = (uint32_t)rsi & UINT32_C(0xf);
    /* 1030: cmp esi, 1 ; 1033: ja 0x1020 */
    if ((uint32_t)rsi > UINT32_C(0x1)) goto L_1020;
L_1035: ;
    /* 1035: add edi, esi */
    rdi = (uint32_t)((uint32_t)rdi + (uint32_t)rsi);
    /* 1037: mov eax, edi */
    rax = (uint32_t)rdi;
    /* 1039: add rsp, 0x28 */
    rsp += UINT64_C(0x28);
    /* 103d: pop rdi */
    rdi = LD64(rsp); rsp += 8;
    /* 103e: pop rsi */
    rsi = LD64(rsp); rsp += 8;
    /* 103f: ret  */
    ctx->r[0] = rax; ctx->r[1] = rcx; ctx->r[4] = rsp; ctx->r[6] = rsi; ctx->r[7] = rdi;
    { RET(0); }
}

/* list_sum (structs.c) */
/* replaces one_structs_1110, lifter text sha256 b82eecb853372b524ee4d719bb2a275ea77abc37f0b4d479ad5cbbe9e0b15938 */
void one_structs_1110(Ctx *ctx) {
    uint64_t rax = ctx->r[0], rcx = ctx->r[1], rdx = ctx->r[2], r8 = ctx->r[8];
    /* 1110: xor edx, edx */
    rdx = 0;
    /* 1112: xor eax, eax */
    rax = 0;
    /* 1114: test rcx, rcx ; 1117: je 0x113a */
    if (rcx == 0) goto L_113a;
L_1120: ;
    /* 1120: mov r8, rax */
    r8 = rax;
    /* 1123: movsxd rax, dword ptr [rcx + 8] */
    rax = (uint64_t)(int64_t)(int32_t)LD32(rcx + UINT64_C(0x8));
    /* 1127: add rax, r8 */
    rax = rax + r8;
    /* 112a: mov rcx, qword ptr [rcx] */
    rcx = LD64(rcx);
    /* 112d: test rcx, rcx ; 1130: je 0x113a */
    if (rcx == 0) goto L_113a;
    /* 1132: cmp edx, 0xf ; 1135: lea edx, [rdx + 1] ; 1138: jb 0x1120 */
    { int below = (uint32_t)rdx < UINT32_C(0xf); rdx = (uint32_t)(rdx + UINT64_C(0x1)); if (below) goto L_1120; }
L_113a: ;
    /* 113a: ret  */
    ctx->r[0] = rax; ctx->r[1] = rcx; ctx->r[2] = rdx; ctx->r[8] = r8;
    { RET(0); }
}
