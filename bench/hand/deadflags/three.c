/* Hand-edited copies of three lifted functions for the gap analysis
 * (docs/findings/25-lifted-speed.md, "Flag computation nobody reads").
 *
 * Not compiled on its own: bench/variants.py (transformation hand:deadflags) substitutes each
 * function below for the lifter's version in a copy of the module's C, if the lifter's text
 * still has the SHA-256 named in the comment above the function.
 *
 * The only change from the lifter's output: a flag write is removed when no instruction can
 * read the flag before it is written again, or before the function returns or calls (the
 * Windows x64 convention leaves the status flags undefined across calls and returns).  A
 * flag that is read is still computed and stored in the Ctx, with the lifter's condition
 * macros reading it; registers and memory accesses are untouched.  Each instruction's
 * comment says which flags survive and why.
 */

/* str_cmp (mem_str.c).  x86: 21 instructions; 8 set flags, of which 4 have a reader. */
/* replaces one_mem_str_1590, lifter text sha256 92ac15183901c3986c4398d19cfe609b9b82b55a28de0b80804302b0fcf5fdd7 */
void one_mem_str_1590(Ctx *ctx) {
    /* 1590: test r8, r8                       ZF read by je; the rest overwritten at 1598 or dead at 15be */
    { uint64_t a = R64(8); ctx->zf = (uint8_t)(a == 0); }
    /* 1593: je 0x15bc */
    { if (CC_E) goto L_15bc; }
    /* 1595: dec r8                            dead: overwritten at 15a5 */
    { W64(8, R64(8) - 1); }
    /* 1598: xor r9d, r9d                      dead: overwritten at 15a5 */
    { W32(9, 0); }
    /* 159b: nop dword ptr [rax + rax] */
L_15a0: ;
    /* 15a0: movzx r10d, byte ptr [rcx + r9] */
    { uint64_t m1 = (R64(1) + R64(9)); W32(10, (uint32_t)LD8(m1)); }
    /* 15a5: cmp r10b, byte ptr [rdx + r9]     ZF read by jne, CF by setae at 15bf */
    { uint64_t m1 = (R64(2) + R64(9)); uint8_t a = R8(10); uint8_t b = LD8(m1);
      ctx->zf = (uint8_t)(a == b); ctx->cf = (uint8_t)(a < b); }
    /* 15a9: jne 0x15bf */
    { if (CC_NE) goto L_15bf; }
    /* 15ab: xor eax, eax                      dead: overwritten at 15ad */
    { W32(0, 0); }
    /* 15ad: test r10b, r10b                   ZF read by je */
    { uint8_t a = R8(10); ctx->zf = (uint8_t)(a == 0); }
    /* 15b0: je 0x15bb */
    { if (CC_E) goto L_15bb; }
    /* 15b2: cmp r8, r9                        ZF read by jne */
    { uint64_t a = R64(8); uint64_t b = R64(9); ctx->zf = (uint8_t)(a == b); }
    /* 15b5: lea r9, [r9 + 1] */
    { W64(9, (R64(9) + UINT64_C(0x1))); }
    /* 15b9: jne 0x15a0 */
    { if (CC_NE) goto L_15a0; }
L_15bb: ;
    /* 15bb: ret  */
    { RET(0); }
L_15bc: ;
    /* 15bc: xor eax, eax                      dead: the function returns */
    { W32(0, 0); }
    /* 15be: ret  */
    { RET(0); }
L_15bf: ;
    /* 15bf: setae al */
    { W8(0, (uint8_t)(CC_AE)); }
    /* 15c2: movzx eax, al */
    { W32(0, (uint32_t)R8(0)); }
    /* 15c5: lea eax, [rax*2 - 1] */
    { W32(0, (uint32_t)(R64(0) * 2 + UINT64_C(0xffffffffffffffff))); }
    /* 15cc: ret  */
    { RET(0); }
}

/* fib (calls.c).  x86: 23 instructions; 11 set flags, of which 2 have a reader. */
/* replaces one_calls_1000, lifter text sha256 98da72712b6d3b19a2c5bd5ddd088bc343299baf7d30f0d93904b02c3913f2b7 */
void one_calls_1000(Ctx *ctx) {
    /* 1000: push rsi */
    { uint64_t v = R64(6); ctx->r[4] -= 8; ST64(ctx->r[4], v); }
    /* 1001: push rdi */
    { uint64_t v = R64(7); ctx->r[4] -= 8; ST64(ctx->r[4], v); }
    /* 1002: sub rsp, 0x28                     dead: overwritten at 1008 */
    { W64(4, R64(4) - UINT64_C(0x28)); }
    /* 1006: mov esi, ecx */
    { W32(6, R32(1)); }
    /* 1008: and esi, 0xf                      dead: overwritten at 100b */
    { W32(6, R32(6) & UINT32_C(0xf)); }
    /* 100b: xor edi, edi                      dead: overwritten at 100d */
    { W32(7, 0); }
    /* 100d: cmp esi, 2                        CF read by jb */
    { uint32_t a = R32(6); ctx->cf = (uint8_t)(a < UINT32_C(0x2)); }
    /* 1010: jb 0x1035 */
    { if (CC_B) goto L_1035; }
    /* 1012: xor edi, edi                      dead: the call at 1023 */
    { W32(7, 0); }
    /* 1014: nop word ptr cs:[rax + rax] */
L_1020: ;
    /* 1020: lea ecx, [rsi - 1] */
    { W32(1, (uint32_t)(R64(6) + UINT64_C(0xffffffffffffffff))); }
    /* 1023: call 0x1000 */
    { CALL_DIRECT(one_calls_1000, 0x1000, 0x1028); }
    /* 1028: add esi, 0xe                      dead: overwritten at 102b */
    { W32(6, R32(6) + UINT32_C(0xe)); }
    /* 102b: add edi, eax                      dead: overwritten at 102d */
    { W32(7, R32(7) + R32(0)); }
    /* 102d: and esi, 0xf                      dead: overwritten at 1030 */
    { W32(6, R32(6) & UINT32_C(0xf)); }
    /* 1030: cmp esi, 1                        CF and ZF read by ja */
    { uint32_t a = R32(6); ctx->cf = (uint8_t)(a < UINT32_C(0x1)); ctx->zf = (uint8_t)(a == UINT32_C(0x1)); }
    /* 1033: ja 0x1020 */
    { if (CC_A) goto L_1020; }
L_1035: ;
    /* 1035: add edi, esi                      dead: overwritten at 1039 */
    { W32(7, R32(7) + R32(6)); }
    /* 1037: mov eax, edi */
    { W32(0, R32(7)); }
    /* 1039: add rsp, 0x28                     dead: the function returns */
    { W64(4, R64(4) + UINT64_C(0x28)); }
    /* 103d: pop rdi */
    { uint64_t v = LD64(ctx->r[4]); ctx->r[4] += 8; W64(7, v); }
    /* 103e: pop rsi */
    { uint64_t v = LD64(ctx->r[4]); ctx->r[4] += 8; W64(6, v); }
    /* 103f: ret  */
    { RET(0); }
}

/* list_sum (structs.c).  x86: 15 instructions; 6 set flags, of which 3 have a reader. */
/* replaces one_structs_1110, lifter text sha256 b82eecb853372b524ee4d719bb2a275ea77abc37f0b4d479ad5cbbe9e0b15938 */
void one_structs_1110(Ctx *ctx) {
    /* 1110: xor edx, edx                      dead: overwritten at 1112 */
    { W32(2, 0); }
    /* 1112: xor eax, eax                      dead: overwritten at 1114 */
    { W32(0, 0); }
    /* 1114: test rcx, rcx                     ZF read by je */
    { uint64_t a = R64(1); ctx->zf = (uint8_t)(a == 0); }
    /* 1117: je 0x113a */
    { if (CC_E) goto L_113a; }
    /* 1119: nop dword ptr [rax] */
L_1120: ;
    /* 1120: mov r8, rax */
    { W64(8, R64(0)); }
    /* 1123: movsxd rax, dword ptr [rcx + 8] */
    { uint64_t m1 = (R64(1) + UINT64_C(0x8)); W64(0, (uint64_t)(int64_t)(int32_t)LD32(m1)); }
    /* 1127: add rax, r8                       dead: overwritten at 112d */
    { W64(0, R64(0) + R64(8)); }
    /* 112a: mov rcx, qword ptr [rcx] */
    { uint64_t m1 = (R64(1)); W64(1, LD64(m1)); }
    /* 112d: test rcx, rcx                     ZF read by je */
    { uint64_t a = R64(1); ctx->zf = (uint8_t)(a == 0); }
    /* 1130: je 0x113a */
    { if (CC_E) goto L_113a; }
    /* 1132: cmp edx, 0xf                      CF read by jb */
    { uint32_t a = R32(2); ctx->cf = (uint8_t)(a < UINT32_C(0xf)); }
    /* 1135: lea edx, [rdx + 1] */
    { W32(2, (uint32_t)(R64(2) + UINT64_C(0x1))); }
    /* 1138: jb 0x1120 */
    { if (CC_B) goto L_1120; }
L_113a: ;
    /* 113a: ret  */
    { RET(0); }
}
