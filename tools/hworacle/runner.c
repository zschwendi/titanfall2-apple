/* runner.c - a real x86-64 CPU as a test oracle for single instructions.
 *
 * Reads one batch of cases from stdin, runs each on the CPU it is running on, writes one result
 * per case to stdout, and exits.  tools/hworacle/README.md describes the format and the rules;
 * tools/hworacle/client.py is the only program that talks to it.
 *
 *   gcc -O2 -static-pie -fno-stack-protector -o runner runner.c      (Linux x86-64, the oracle)
 *   cc -O1 -DHWO_CHECK_TOOL -o refuse_check runner.c                 (anywhere: the byte inspection only)
 *
 * How a case runs.  The parent maps the case's memory at the addresses the case names, forks, and
 * the child - after entering seccomp strict mode - executes `int3`.  The SIGTRAP handler saves
 * the runner's complete register state from the signal frame and overwrites the frame with the
 * guest's (all sixteen general registers, RIP, RFLAGS with the trap flag set, XMM0-15, MXCSR, x87
 * and the upper halves of YMM in their initial state), so the kernel's sigreturn loads all of it
 * at once and nothing of the runner's is visible to the guest.  The guest executes one
 * instruction; the single-step trap (or the fault) comes back to the same handler, which copies
 * the guest state out of the new frame and puts the runner's state back.  The runner continues
 * after its `int3` as if nothing had happened.
 *
 * What protects the machine.  (1) Byte inspection: an instruction that is not on the allow-list
 * below is refused before it runs (hwo_decode).  (2) seccomp strict mode in the child, entered
 * before any guest instruction: the only system calls left are read, write, _exit and sigreturn,
 * anything else is SIGKILL.  The child holds one file descriptor, the pipe to its parent.
 * (3) The parent waits for each result with a time limit and kills a child that is late; the
 * parent itself dies at a whole-batch deadline (alarm), and a child whose parent dies is killed
 * by the kernel (PR_SET_PDEATHSIG).  The parent never executes a guest instruction.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HWO_FORMAT_VERSION 1
/* Bump when the same case could get a different answer from a new runner (a fixed bug, a changed
 * rule).  It is part of every cache key, so old answers stop being used. */
#define HWO_ANSWERS_REV 1

#ifndef HWO_BUILD_ID
#define HWO_BUILD_ID "dev"
#endif

/* ================================================================================================
 * Part 1 (portable C): what an instruction's bytes say, and whether it may run.
 * ============================================================================================== */

enum {
    HWO_OK = 0,              /* may run; execution continues at the next instruction */
    HWO_CTL = 1,             /* may run if the case says where it is expected to stop */
    /* refusals */
    HWO_R_UNLISTED = 16,     /* not on the allow-list (unknown, or nobody needed it yet) */
    HWO_R_SYSCALL = 17,      /* syscall, sysenter, sysret, sysexit */
    HWO_R_INT = 18,          /* int n, int1 (int3 is allowed: it is a plain trap) */
    HWO_R_HLT = 19,
    HWO_R_IO = 20,           /* in, out, ins, outs */
    HWO_R_PRIV = 21,         /* privileged or system: mov cr/dr, lgdt, rdmsr, cli, ... */
    HWO_R_NONDET = 22,       /* the answer is the machine's, not the instruction's: rdtsc, cpuid, rdrand, ... */
    HWO_R_SEG = 23,          /* loads or reads a segment register, or uses fs: (its base is not part of a case) */
    HWO_R_X87MMX = 24,       /* x87 or MMX: that state is not part of a case in format 1 */
    HWO_R_VEX = 25,          /* VEX, EVEX, XOP: the upper halves of YMM are not part of a case in format 1 */
    HWO_R_FAR = 26,          /* far call, far jump, far return, iret */
    HWO_R_INVALID64 = 27,    /* not an instruction in 64-bit mode */
    HWO_R_TRUNC = 28,        /* the bytes end before the instruction does, or it is longer than 15 bytes */
    HWO_R_PREFIX = 29,       /* a prefix combination whose meaning this decoder does not claim to know */
    HWO_R_STATE = 30,        /* fxsave, xsave, ...: dumps or loads state that is not part of a case */
    HWO_R_CTL_NO_STOP = 31,  /* a control transfer in a case that lists no stop address */
    HWO_R_NOT_EXEC = 32,     /* the start address is not inside an executable region of the case */
    HWO_R_PROGRAM = 33,      /* free-running case: the program failed the static check */
    HWO_R_UNCHECKED = 34,    /* the case asks to skip inspection but the runner was not started with --allow-unchecked */
};

struct hwo_insn {
    uint8_t len;             /* length in bytes; 0 if refused */
    uint8_t cls;             /* HWO_OK, HWO_CTL or a refusal */
    uint8_t rep_string;      /* REP/REPNE string instruction: one single-step trap per iteration */
    uint8_t pushf;           /* pushfq: bytes pushed (2 or 8), else 0 */
    uint8_t direct;          /* HWO_CTL with a relative target (jcc, jmp, loop, jrcxz, call rel32) */
    uint8_t uncond;          /* jmp rel: execution never falls through */
    uint8_t call_ret_ind;    /* call, ret, or an indirect jump: not allowed in a free-running program */
    int32_t rel;             /* displacement of a direct branch */
};

#define F_M     0x01         /* ModRM follows */
#define F_I8    0x02         /* 8-bit immediate */
#define F_IZ    0x04         /* 16- or 32-bit immediate (operand size) */
#define F_IV    0x08         /* 16-, 32- or 64-bit immediate (mov r, imm) */
#define F_I16   0x10         /* 16-bit immediate */
#define F_MOFFS 0x20         /* 64-bit (32 with 67h) absolute address */
#define F_REL8  0x40
#define F_REL32 0x80

/* One-byte opcode map, 64-bit mode.  `reg` is ModRM.reg where the opcode is a group (-1 if the
 * ModRM byte lies beyond the bytes given). */
static int hwo_map1(uint8_t op, int reg, unsigned *fl, struct hwo_insn *o)
{
    *fl = 0;
    if (op < 0x40) {                                   /* add or adc sbb and sub xor cmp */
        unsigned lo = op & 7;
        if (lo < 4) { *fl = F_M; return HWO_OK; }
        if (lo == 4) { *fl = F_I8; return HWO_OK; }
        if (lo == 5) { *fl = F_IZ; return HWO_OK; }
        return HWO_R_INVALID64;                        /* push/pop of a segment register, daa, aaa, ... */
    }
    if (op >= 0x50 && op <= 0x5F) return HWO_OK;       /* push r, pop r */
    if (op >= 0x70 && op <= 0x7F) { *fl = F_REL8; o->direct = 1; return HWO_CTL; }
    if (op >= 0x91 && op <= 0x97) return HWO_OK;       /* xchg rax, r */
    if (op >= 0xB0 && op <= 0xB7) { *fl = F_I8; return HWO_OK; }
    if (op >= 0xB8 && op <= 0xBF) { *fl = F_IV; return HWO_OK; }
    if (op >= 0xD8 && op <= 0xDF) return HWO_R_X87MMX;
    switch (op) {
    case 0x60: case 0x61: case 0x82: case 0x9A: case 0xCE: case 0xD4: case 0xD5: case 0xD6: case 0xEA:
        return HWO_R_INVALID64;
    case 0x62: return HWO_R_VEX;                       /* EVEX */
    case 0x63: *fl = F_M; return HWO_OK;               /* movsxd */
    case 0x68: *fl = F_IZ; return HWO_OK;              /* push imm */
    case 0x69: *fl = F_M | F_IZ; return HWO_OK;        /* imul r, r/m, imm */
    case 0x6A: *fl = F_I8; return HWO_OK;
    case 0x6B: *fl = F_M | F_I8; return HWO_OK;
    case 0x6C: case 0x6D: case 0x6E: case 0x6F: return HWO_R_IO;
    case 0x80: case 0x83: *fl = F_M | F_I8; return HWO_OK;
    case 0x81: *fl = F_M | F_IZ; return HWO_OK;
    case 0x84: case 0x85: case 0x86: case 0x87:        /* test, xchg */
    case 0x88: case 0x89: case 0x8A: case 0x8B:        /* mov */
    case 0x8D:                                         /* lea */
        *fl = F_M; return HWO_OK;
    case 0x8C: case 0x8E: return HWO_R_SEG;
    case 0x8F: *fl = F_M; return reg == 0 ? HWO_OK : HWO_R_VEX;      /* pop r/m; otherwise XOP */
    case 0x90: return HWO_OK;                          /* nop, pause */
    case 0x98: case 0x99: return HWO_OK;               /* cbw.., cwd.. */
    case 0x9B: return HWO_R_X87MMX;                    /* fwait */
    case 0x9C: o->pushf = 8; return HWO_OK;            /* pushfq (see the trap-flag note in the handler) */
    case 0x9D: case 0x9E: case 0x9F: return HWO_OK;    /* popfq, sahf, lahf */
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: *fl = F_MOFFS; return HWO_OK;
    case 0xA4: case 0xA5: case 0xA6: case 0xA7:        /* movs, cmps */
    case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:   /* stos, lods, scas */
        o->rep_string = 1; return HWO_OK;              /* cleared by the caller when there is no REP */
    case 0xA8: *fl = F_I8; return HWO_OK;
    case 0xA9: *fl = F_IZ; return HWO_OK;
    case 0xC0: case 0xC1: *fl = F_M | F_I8; return HWO_OK;
    case 0xC2: *fl = F_I16; o->call_ret_ind = 1; return HWO_CTL;
    case 0xC3: o->call_ret_ind = 1; return HWO_CTL;
    case 0xC4: case 0xC5: return HWO_R_VEX;
    case 0xC6: *fl = F_M | F_I8; return reg == 0 ? HWO_OK : HWO_R_UNLISTED;   /* C6 F8 is xabort */
    case 0xC7: *fl = F_M | F_IZ; return reg == 0 ? HWO_OK : HWO_R_UNLISTED;   /* C7 F8 is xbegin */
    case 0xC8: *fl = F_I16 | F_I8; return HWO_OK;      /* enter */
    case 0xC9: return HWO_OK;                          /* leave */
    case 0xCA: case 0xCB: case 0xCF: return HWO_R_FAR;
    case 0xCC: return HWO_OK;                          /* int3: a trap, reported as a fault */
    case 0xCD: case 0xF1: return HWO_R_INT;
    case 0xD0: case 0xD1: case 0xD2: case 0xD3: *fl = F_M; return HWO_OK;
    case 0xD7: return HWO_OK;                          /* xlat */
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: *fl = F_REL8; o->direct = 1; return HWO_CTL;
    case 0xE4: case 0xE5: case 0xE6: case 0xE7: case 0xEC: case 0xED: case 0xEE: case 0xEF: return HWO_R_IO;
    case 0xE8: *fl = F_REL32; o->direct = 1; o->call_ret_ind = 1; return HWO_CTL;
    case 0xE9: *fl = F_REL32; o->direct = 1; o->uncond = 1; return HWO_CTL;
    case 0xEB: *fl = F_REL8; o->direct = 1; o->uncond = 1; return HWO_CTL;
    case 0xF4: return HWO_R_HLT;
    case 0xF5: case 0xF8: case 0xF9: case 0xFC: case 0xFD: return HWO_OK;     /* cmc clc stc cld std */
    case 0xFA: case 0xFB: return HWO_R_PRIV;           /* cli, sti */
    case 0xF6: *fl = F_M | (reg >= 0 && reg < 2 ? F_I8 : 0); return HWO_OK;
    case 0xF7: *fl = F_M | (reg >= 0 && reg < 2 ? F_IZ : 0); return HWO_OK;
    case 0xFE: *fl = F_M; return reg == 0 || reg == 1 ? HWO_OK : HWO_R_UNLISTED;
    case 0xFF:
        *fl = F_M;
        if (reg == 0 || reg == 1 || reg == 6) return HWO_OK;                 /* inc, dec, push */
        if (reg == 2 || reg == 4) { o->call_ret_ind = 1; return HWO_CTL; }   /* call, jmp through r/m */
        if (reg == 3 || reg == 5) return HWO_R_FAR;
        return HWO_R_UNLISTED;
    }
    return HWO_R_UNLISTED;
}

/* The SSE columns of the two-byte map are selected by the mandatory prefix: none, 66, F3, F2. */
#define NP  1u
#define P66 2u
#define PF3 4u
#define PF2 8u

/* 0F xx.  `pfx` is one of NP, P66, PF3, PF2; `mixed` says 66 came together with F2/F3. */
static int hwo_map0f(uint8_t op, unsigned pfx, int mixed, int mod, int reg, unsigned *fl, struct hwo_insn *o)
{
#define SSE(ok)        do { *fl |= F_M; if (mixed) return HWO_R_PREFIX; return ((ok) & pfx) ? HWO_OK : HWO_R_UNLISTED; } while (0)
#define SSE_MMX(ok)    do { *fl |= F_M; if (mixed) return HWO_R_PREFIX; \
                            return ((ok) & pfx) ? HWO_OK : pfx == NP ? HWO_R_X87MMX : HWO_R_UNLISTED; } while (0)
#define GPR_M()        do { *fl |= F_M; return (pfx & (NP | P66)) ? HWO_OK : HWO_R_PREFIX; } while (0)
    *fl = 0;
    if (op >= 0x40 && op <= 0x4F) GPR_M();             /* cmovcc */
    if (op >= 0x80 && op <= 0x8F) {                    /* jcc rel32 */
        *fl = F_REL32; o->direct = 1;
        return pfx == NP ? HWO_CTL : HWO_R_PREFIX;
    }
    if (op >= 0x90 && op <= 0x9F) GPR_M();             /* setcc */
    if (op >= 0xC8 && op <= 0xCF) return pfx == NP ? HWO_OK : HWO_R_PREFIX;   /* bswap (16-bit form is undefined) */
    switch (op) {
    case 0x00: case 0x01: case 0x02: case 0x03: return HWO_R_PRIV;   /* sldt.., sgdt.., xgetbv, rdtscp, swapgs, lar, lsl */
    case 0x05: case 0x07: case 0x34: case 0x35: return HWO_R_SYSCALL;
    case 0x06: case 0x08: case 0x09: case 0x30: case 0x32: case 0x37: return HWO_R_PRIV;
    case 0x0B: return HWO_OK;                          /* ud2: #UD, reported as a fault */
    case 0x0D: *fl = F_M; return pfx == NP ? HWO_OK : HWO_R_PREFIX;   /* prefetchw */
    case 0x0E: case 0x0F: case 0x77: return HWO_R_X87MMX;             /* femms, 3DNow!, emms */
    case 0x10: case 0x11: case 0x12: SSE(NP | P66 | PF3 | PF2);      /* movups movss movupd movsd; movlps movsldup movddup */
    case 0x13: case 0x14: case 0x15: case 0x17: SSE(NP | P66);       /* movlps store, unpck, movhps store */
    case 0x16: SSE(NP | P66 | PF3);                                  /* movhps, movshdup */
    case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
        GPR_M();                                       /* prefetch hints and the reserved-nop block (no CET, no MPX here) */
    case 0x20: case 0x21: case 0x22: case 0x23: return HWO_R_PRIV;   /* mov cr, mov dr */
    case 0x28: case 0x29: case 0x2B: case 0x2E: case 0x2F: SSE(NP | P66);   /* movaps, movntps, ucomis, comis */
    case 0x2A: case 0x2C: case 0x2D: SSE_MMX(PF3 | PF2);             /* cvtsi2ss/sd, cvtt/cvt ss/sd 2si */
    case 0x31: case 0x33: case 0xA2: return HWO_R_NONDET;            /* rdtsc, rdpmc, cpuid */
    case 0x50: SSE(NP | P66);                                        /* movmskps/pd */
    case 0x51: case 0x58: case 0x59: case 0x5A: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        SSE(NP | P66 | PF3 | PF2);                     /* sqrt add mul cvt sub min div max */
    case 0x52: case 0x53: SSE(NP | PF3);                             /* rsqrt, rcp */
    case 0x54: case 0x55: case 0x56: case 0x57: SSE(NP | P66);       /* and andn or xor */
    case 0x5B: SSE(NP | P66 | PF3);                                  /* cvtdq2ps cvtps2dq cvttps2dq */
    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
    case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6E:
        SSE_MMX(P66);                                  /* punpck, packs, pcmpgt, movd/movq */
    case 0x6C: case 0x6D: SSE(P66);                                  /* punpcklqdq, punpckhqdq */
    case 0x6F: case 0x7E: case 0x7F: SSE_MMX(P66 | PF3);             /* movdqa movdqu, movd/movq */
    case 0x70: *fl = F_I8; SSE_MMX(P66 | PF3 | PF2);                 /* pshufd pshufhw pshuflw */
    case 0x71: case 0x72: case 0x73:                                 /* shifts by an immediate */
        *fl = F_I8;
        if (mod != 3) { *fl |= F_M; return HWO_R_UNLISTED; }
        SSE_MMX(P66);
    case 0x74: case 0x75: case 0x76: SSE_MMX(P66);                   /* pcmpeq */
    case 0x78: case 0x79: return HWO_R_PRIV;                         /* vmread, vmwrite */
    case 0x7C: case 0x7D: case 0xD0: SSE(P66 | PF2);                 /* hadd, hsub, addsub */
    case 0xA0: case 0xA1: case 0xA8: case 0xA9: case 0xB2: case 0xB4: case 0xB5: return HWO_R_SEG;
    case 0xA3: case 0xAB: case 0xB3: case 0xBB: GPR_M();             /* bt bts btr btc */
    case 0xA4: case 0xAC: *fl = F_I8; GPR_M();                       /* shld, shrd by imm8 */
    case 0xA5: case 0xAD: case 0xAF: GPR_M();                        /* shld/shrd by cl, imul */
    case 0xAA: return HWO_R_PRIV;                                    /* rsm */
    case 0xAE:
        *fl = F_M;
        if (pfx == PF3) return HWO_R_SEG;                            /* rdfsbase, rdgsbase, wrfsbase, wrgsbase */
        if (pfx != NP) return HWO_R_UNLISTED;                        /* clflushopt, clwb, ... */
        if (mod == 3) return reg >= 5 ? HWO_OK : HWO_R_UNLISTED;     /* lfence, mfence, sfence */
        if (reg == 2 || reg == 3) return HWO_OK;                     /* ldmxcsr, stmxcsr */
        return reg == 7 ? HWO_R_UNLISTED : HWO_R_STATE;              /* clflush; fxsave fxrstor xsave xrstor xsaveopt */
    case 0xB0: case 0xB1: case 0xB6: case 0xB7: case 0xBE: case 0xBF: case 0xC0: case 0xC1:
        GPR_M();                                       /* cmpxchg, movzx, movsx, xadd */
    case 0xB8: *fl = F_M; return pfx == PF3 ? HWO_OK : HWO_R_UNLISTED;          /* popcnt */
    case 0xBA: *fl = F_M | F_I8; return (pfx & (NP | P66)) && reg >= 4 ? HWO_OK : HWO_R_UNLISTED;
    case 0xBC: case 0xBD: *fl = F_M; return pfx == PF2 ? HWO_R_PREFIX : HWO_OK;   /* bsf bsr; tzcnt lzcnt with F3 */
    case 0xC2: *fl = F_I8; SSE(NP | P66 | PF3 | PF2);                /* cmpps cmppd cmpss cmpsd */
    case 0xC3: SSE(NP);                                              /* movnti */
    case 0xC4: case 0xC5: *fl = F_I8; SSE_MMX(P66);                  /* pinsrw, pextrw */
    case 0xC6: *fl = F_I8; SSE(NP | P66);                            /* shufps, shufpd */
    case 0xC7:
        *fl = F_M;
        if (reg == 6 || reg == 7) return mod == 3 ? HWO_R_NONDET : HWO_R_PRIV;   /* rdrand, rdseed; vmptrld.. */
        return pfx == NP && mod != 3 && reg == 1 ? HWO_OK : HWO_R_UNLISTED;      /* cmpxchg8b, cmpxchg16b */
    case 0xD1: case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD7:
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: case 0xE7:
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF:
    case 0xF1: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF7:
    case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE:
        SSE_MMX(P66);                                  /* the packed-integer block */
    case 0xD6:                                         /* movq xmm/m64, xmm; movq2dq and movdq2q are MMX */
        *fl = F_M;
        if (mixed) return HWO_R_PREFIX;
        return pfx == P66 ? HWO_OK : pfx == NP ? HWO_R_UNLISTED : HWO_R_X87MMX;
    case 0xE6: SSE(P66 | PF3 | PF2);                                 /* cvttpd2dq cvtdq2pd cvtpd2dq */
    case 0xF0: SSE(PF2);                                             /* lddqu */
    }
    return HWO_R_UNLISTED;
#undef GPR_M
}

static int hwo_map0f38(uint8_t op, unsigned pfx, int mixed, unsigned *fl)
{
    *fl = F_M;
    if (op == 0xF0 || op == 0xF1) {                    /* movbe (none, 66), crc32 (F2, and 66 F2 for r/m16) */
        if (pfx == PF3) return HWO_R_UNLISTED;
        return mixed && !(pfx == PF2 && op == 0xF1) ? HWO_R_PREFIX : HWO_OK;
    }
    if (mixed) return HWO_R_PREFIX;
    if (pfx == NP) return (op <= 0x0B || (op >= 0x1C && op <= 0x1E)) ? HWO_R_X87MMX : HWO_R_UNLISTED;
    if (pfx != P66) return HWO_R_UNLISTED;
    if (op <= 0x0B || op == 0x10 || op == 0x14 || op == 0x15 || op == 0x17 || (op >= 0x1C && op <= 0x1E)
        || (op >= 0x20 && op <= 0x25) || (op >= 0x28 && op <= 0x2B) || (op >= 0x30 && op <= 0x35)
        || (op >= 0x37 && op <= 0x41) || (op >= 0xDB && op <= 0xDF))
        return HWO_OK;                                 /* SSSE3, SSE4.1, SSE4.2 pcmpgtq, AES */
    return HWO_R_UNLISTED;
}

static int hwo_map0f3a(uint8_t op, unsigned pfx, int mixed, unsigned *fl)
{
    *fl = F_M | F_I8;
    if (mixed) return HWO_R_PREFIX;
    if (pfx == NP) return op == 0x0F ? HWO_R_X87MMX : HWO_R_UNLISTED;
    if (pfx != P66) return HWO_R_UNLISTED;
    if ((op >= 0x08 && op <= 0x0F) || (op >= 0x14 && op <= 0x17) || (op >= 0x20 && op <= 0x22)
        || (op >= 0x40 && op <= 0x42) || op == 0x44 || (op >= 0x60 && op <= 0x63) || op == 0xDF)
        return HWO_OK;                                 /* round, blend, palignr, pextr, pinsr, dp, pclmul, pcmpXstrY, aeskeygen */
    return HWO_R_UNLISTED;
}

/* Decode the instruction at p[0..avail).  Returns o->cls.  o->len is exact when the class is
 * HWO_OK or HWO_CTL, and 0 otherwise. */
static int hwo_decode(const uint8_t *p, unsigned avail, struct hwo_insn *o)
{
    unsigned i = 0, fl = 0, n66 = 0, n67 = 0, rep = 0, fs = 0, rex = 0, pfx, len;
    int cls, mod = -1, reg = -1, mixed;
    memset(o, 0, sizeof *o);
    if (avail > 15) avail = 15;
#define NEED(n) do { if (i + (n) > avail) { o->cls = HWO_R_TRUNC; return o->cls; } } while (0)
    for (;;) {
        NEED(1);
        uint8_t b = p[i];
        if (b == 0x66) n66 = 1;
        else if (b == 0x67) n67 = 1;
        else if (b == 0xF2 || b == 0xF3) rep = b;      /* the last one wins */
        else if (b == 0x64) fs = 1;
        else if (!(b == 0xF0 || b == 0x65 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26)) break;
        i++;
    }
    if ((p[i] & 0xF0) == 0x40) { rex = p[i]; i++; NEED(1); }
    pfx = rep == 0xF3 ? PF3 : rep == 0xF2 ? PF2 : n66 ? P66 : NP;
    mixed = n66 && rep;
    if (p[i] != 0x0F) {
        uint8_t op = p[i++];
        if (i < avail) reg = (p[i] >> 3) & 7;
        cls = hwo_map1(op, reg, &fl, o);
        if (!rep) o->rep_string = 0;
        if (o->pushf && n66) o->pushf = 2;
        if (cls == HWO_CTL && n66) cls = HWO_R_PREFIX;   /* 66 on a near branch: Intel and AMD disagree on the length */
    } else {
        i++; NEED(1);
        uint8_t op = p[i++];
        if (op == 0x38 || op == 0x3A) {
            NEED(1);
            uint8_t op3 = p[i++];
            cls = op == 0x38 ? hwo_map0f38(op3, pfx, mixed, &fl) : hwo_map0f3a(op3, pfx, mixed, &fl);
        } else {
            if (i < avail) { mod = p[i] >> 6; reg = (p[i] >> 3) & 7; }
            cls = hwo_map0f(op, pfx, mixed, mod, reg, &fl, o);
            if ((op == 0xB8 || op == 0xBC || op == 0xBD) && cls == HWO_R_PREFIX && pfx == PF3) cls = HWO_OK;
        }
    }
    if (cls >= HWO_R_UNLISTED) goto refuse;
    if (fs) { cls = HWO_R_SEG; goto refuse; }
    len = i;
    if (fl & F_M) {
        if (len + 1 > avail) { cls = HWO_R_TRUNC; goto refuse; }
        uint8_t m = p[len++];
        unsigned md = m >> 6, rm = m & 7;
        if (md != 3) {
            unsigned base5 = 0;
            if (rm == 4) {
                if (len + 1 > avail) { cls = HWO_R_TRUNC; goto refuse; }
                base5 = (p[len++] & 7) == 5;
            }
            if (md == 1) len += 1;
            else if (md == 2 || (md == 0 && (rm == 5 || base5))) len += 4;
        }
    }
    if (fl & F_I8) len += 1;
    if (fl & F_I16) len += 2;
    if (fl & F_IZ) len += (n66 && !(rex & 8)) ? 2 : 4;
    if (fl & F_IV) len += (rex & 8) ? 8 : n66 ? 2 : 4;
    if (fl & F_MOFFS) len += n67 ? 4 : 8;
    if (fl & F_REL8) {
        if (len + 1 > avail) { cls = HWO_R_TRUNC; goto refuse; }
        o->rel = (int8_t)p[len]; len += 1;
    }
    if (fl & F_REL32) {
        if (len + 4 > avail) { cls = HWO_R_TRUNC; goto refuse; }
        o->rel = (int32_t)((uint32_t)p[len] | (uint32_t)p[len + 1] << 8 | (uint32_t)p[len + 2] << 16 | (uint32_t)p[len + 3] << 24);
        len += 4;
    }
    if (len > avail) { cls = HWO_R_TRUNC; goto refuse; }
    o->len = (uint8_t)len;
    o->cls = (uint8_t)cls;
    return cls;
refuse:
    memset(o, 0, sizeof *o);
    o->cls = (uint8_t)cls;
    return cls;
#undef NEED
}

/* Free-running programs (max_insns = 0) run without the trap flag, so every instruction that can
 * execute has to be inspected beforehand: the code extent is decoded front to back, every
 * instruction must be allowed, and every branch must be direct and land on an instruction of the
 * extent or on a stop address.  No call, no ret, no indirect jump.  The caller checks that the
 * extent is not writable. */
#define HWO_MAX_PROGRAM 4096
static int hwo_check_program(const uint8_t *code, unsigned code_len, uint64_t addr, const uint64_t *stops, unsigned n_stops)
{
    static uint8_t start[HWO_MAX_PROGRAM + 1];
    static uint64_t target[HWO_MAX_PROGRAM];
    unsigned off = 0, nt = 0, k, s, last_uncond = 0;
    if (code_len == 0 || code_len > HWO_MAX_PROGRAM || n_stops == 0) return HWO_R_PROGRAM;
    memset(start, 0, sizeof start);
    while (off < code_len) {
        struct hwo_insn in;
        int cls = hwo_decode(code + off, code_len - off, &in);
        if (cls >= HWO_R_UNLISTED) return cls;
        if (cls == HWO_CTL) {
            if (!in.direct || in.call_ret_ind) return HWO_R_PROGRAM;
            target[nt++] = addr + off + in.len + (int64_t)in.rel;
        }
        start[off] = 1;
        last_uncond = in.uncond;
        off += in.len;
    }
    if (off != code_len) return HWO_R_PROGRAM;
    if (!last_uncond) target[nt++] = addr + code_len;                /* falling off the end */
    for (k = 0; k < nt; k++) {
        if (target[k] >= addr && target[k] < addr + code_len && start[target[k] - addr]) continue;
        for (s = 0; s < n_stops; s++)
            if (stops[s] == target[k]) break;
        if (s == n_stops) return HWO_R_PROGRAM;
    }
    return HWO_OK;
}

/* ---- SHA-256 (FIPS 180-4), for the memory blobs --------------------------------------------- */

static const uint32_t sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

static void sha_block(uint32_t h[8], const uint8_t *b)
{
#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))
    uint32_t w[64], v[8], t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
    for (; i < 64; i++)
        w[i] = w[i - 16] + (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7]
             + (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10));
    memcpy(v, h, sizeof v);
    for (i = 0; i < 64; i++) {
        t1 = v[7] + (ROR(v[4], 6) ^ ROR(v[4], 11) ^ ROR(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + sha_k[i] + w[i];
        t2 = (ROR(v[0], 2) ^ ROR(v[0], 13) ^ ROR(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1; v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++) h[i] += v[i];
#undef ROR
}

static void hwo_sha256(const uint8_t *data, uint64_t len, uint8_t out[32])
{
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t tail[128];
    uint64_t n = len / 64, r = len % 64, bits = len * 8, i;
    for (i = 0; i < n; i++) sha_block(h, data + 64 * i);
    memset(tail, 0, sizeof tail);
    if (r) memcpy(tail, data + 64 * n, r);
    tail[r] = 0x80;
    n = r < 56 ? 64 : 128;
    for (i = 0; i < 8; i++) tail[n - 1 - i] = (uint8_t)(bits >> (8 * i));
    sha_block(h, tail);
    if (n == 128) sha_block(h, tail + 64);
    for (i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24); out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8); out[4 * i + 3] = (uint8_t)h[i];
    }
}

#ifdef HWO_CHECK_TOOL
/* ================================================================================================
 * The byte inspection alone, for tests on any machine.
 *   refuse_check            lines of hex on stdin -> "len cls rep_string pushf direct uncond call_ret_ind rel"
 *   refuse_check program    lines "addr_hex code_hex stop_hex[,stop_hex...]" -> the static check's verdict
 *   refuse_check sha256     lines of hex -> the digest
 *   refuse_check version    the format version and the answers revision
 * ============================================================================================== */
static unsigned unhex(const char *s, uint8_t *out, unsigned cap)
{
    unsigned n = 0;
    while (s[0] && s[1] && n < cap) {
        unsigned v;
        if (sscanf(s, "%2x", &v) != 1) break;
        out[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}

int main(int argc, char **argv)
{
    static char line[3 * HWO_MAX_PROGRAM];
    static uint8_t buf[HWO_MAX_PROGRAM + 64];
    const char *mode = argc > 1 ? argv[1] : "";
    if (!strcmp(mode, "version")) {
        printf("%d %d\n", HWO_FORMAT_VERSION, HWO_ANSWERS_REV);
        return 0;
    }
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(mode, "program")) {
            char code[2 * HWO_MAX_PROGRAM + 2], stops_s[1024];
            unsigned long long addr;
            uint64_t stops[16];
            unsigned ns = 0;
            if (sscanf(line, "%llx %8192s %1023s", &addr, code, stops_s) != 3) { puts("?"); continue; }
            for (char *t = strtok(stops_s, ","); t && ns < 16; t = strtok(NULL, ","))
                stops[ns++] = strtoull(t, NULL, 16);
            unsigned n = unhex(code, buf, HWO_MAX_PROGRAM + 1);
            printf("%d\n", hwo_check_program(buf, n, addr, stops, ns));
        } else if (!strcmp(mode, "sha256")) {
            uint8_t d[32];
            unsigned n = unhex(line, buf, sizeof buf);
            hwo_sha256(buf, n, d);
            for (int i = 0; i < 32; i++) printf("%02x", d[i]);
            putchar('\n');
        } else {
            struct hwo_insn in;
            unsigned n = unhex(line, buf, 32);
            hwo_decode(buf, n, &in);
            printf("%u %u %u %u %u %u %u %d\n", in.len, in.cls, in.rep_string, in.pushf, in.direct, in.uncond,
                   in.call_ret_ind, in.rel);
        }
    }
    return 0;
}

#else
/* ================================================================================================
 * Part 2 (Linux x86-64 only): the runner.
 * ============================================================================================== */
#if !defined(__x86_64__) || !defined(__linux__)
#error "the runner is for Linux on x86-64; build with -DHWO_CHECK_TOOL elsewhere"
#endif

#include <cpuid.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <asm/prctl.h>
#include <linux/seccomp.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/personality.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

/* ---- the wire format (little-endian; tools/hworacle/README.md) ------------------------------- */

#define MAGIC_BATCH   0x424F5748u    /* "HWOB" */
#define MAGIC_CASES   0x434F5748u    /* "HWOC": after the tables, before the cases */
#define MAGIC_END_IN  0x5A4F5748u    /* "HWOZ": after the last case */
#define MAGIC_RESULT  0x524F5748u    /* "HWOR" */
#define MAGIC_END_OUT 0x454F5748u    /* "HWOE" */

struct batch_hdr {                   /* 48 bytes */
    uint32_t magic, version, flags;
    uint32_t time_limit_ms;          /* per case; 0 = 2000 */
    uint32_t group_size;             /* cases per forked child; 0 = 64; 1 = a child per case */
    uint32_t batch_limit_s;          /* the whole batch; 0 = 120 */
    uint32_t n_blobs, n_sets, n_cases;
    uint32_t reserved[3];
};
struct blob_hdr {                    /* 48 bytes; `len` bytes follow, padded to 8, when where == 0 */
    uint8_t sha256[32];
    uint64_t len;
    uint32_t where;                  /* 0 inline, 1 file <blob dir>/<sha256 hex> */
    uint32_t reserved;
};
struct region_rec {                  /* 24 bytes */
    uint64_t addr, size;             /* 4 KiB aligned */
    uint32_t prot;                   /* 1 read, 2 write, 4 execute */
    uint32_t blob;                   /* index; 0xFFFFFFFF = zero-filled; shorter than size = zero-padded */
};
#define CASE_UNCHECKED 1u            /* skip the byte inspection (tests of the sandbox only; needs --allow-unchecked) */
struct case_hdr {                    /* 440 bytes, then stops[n_stops] (u64), then the pokes */
    uint32_t rec_len;                /* bytes after this field */
    uint32_t flags;
    uint32_t set;                    /* which region set */
    uint32_t max_insns;              /* 1: one instruction.  N > 1: N single steps, each inspected.  0: free-running */
    uint32_t n_stops, n_pokes;
    uint32_t code_len;               /* free-running only: the extent of the program, from rip */
    uint32_t mxcsr;
    uint64_t rip, rflags, gs_base;
    uint64_t gpr[16];                /* rax rcx rdx rbx rsp rbp rsi rdi r8..r15 */
    uint8_t xmm[16][16];
};
struct poke_hdr { uint64_t addr; uint32_t len, reserved; };          /* then len bytes, padded to 8 */

enum { OUT_DONE = 0, OUT_FAULT = 1, OUT_REFUSED = 2, OUT_TIMEOUT = 3, OUT_KILLED = 4, OUT_BAD_CASE = 5, OUT_RUNNER_ERROR = 6 };
#define DONE_AT_STOP        1u       /* rip is one of the case's stop addresses */
#define DONE_UNEXPECTED_RIP 2u       /* rip is neither the next instruction nor a stop address */
#define DETAIL_MEM_TRUNCATED 0x100u  /* more memory changed than fits in a record; the runs given are the lowest */
enum { BAD_SET = 1, BAD_MXCSR, BAD_GS, BAD_POKE, BAD_STOP, BAD_RECORD, BAD_SANDBOX, BAD_LIMITS };

struct result_hdr {                  /* 312 bytes */
    uint32_t magic, version;
    uint32_t status;                 /* 0, or a batch-level failure (enum below); `message` says what */
    uint32_t n_results;              /* result records that follow (status 0), or 32-byte blob digests (BATCH_MISSING_BLOBS) */
    uint32_t cpu_signature;          /* cpuid(1).eax */
    uint32_t microcode;
    uint32_t flags;                  /* 1: address-space randomisation off; 2: FSGSBASE; */
    uint32_t answers_rev;
    uint64_t xcr0;
    char brand[48], kernel[64], build[32], message[128];
};
enum { BATCH_OK = 0, BATCH_BAD_INPUT = 1, BATCH_MISSING_BLOBS = 2, BATCH_SELFTEST_FAILED = 3, BATCH_INTERNAL = 4 };
#define RH_NO_ASLR 1u
#define RH_FSGSBASE 2u

struct res_hdr {                     /* 72 bytes, then gpr values, xmm values, memory runs */
    uint32_t rec_len;                /* bytes after this field */
    uint32_t index;                  /* which case of the batch */
    uint32_t outcome, detail;
    int32_t signo, sig_code;         /* OUT_FAULT: the signal and its si_code */
    uint32_t trapno, err;            /* the CPU's exception number and error code (page fault: only bit 1 write, bit 4 fetch) */
    uint64_t sig_addr;               /* si_addr: the address for a page fault, the instruction for #DE and #UD */
    uint64_t rip, rflags;            /* rflags: CF PF AF ZF SF DF OF only */
    uint32_t mxcsr;
    uint32_t insns;                  /* instructions completed */
    uint16_t gpr_mask, xmm_mask;     /* registers whose value differs from the case's; their values follow in index order */
    uint32_t n_mem;                  /* runs of changed bytes: {u64 addr; u32 len; u32 0; bytes padded to 8} */
};

_Static_assert(sizeof(struct batch_hdr) == 48 && sizeof(struct blob_hdr) == 48 && sizeof(struct region_rec) == 24
               && sizeof(struct case_hdr) == 440 && sizeof(struct poke_hdr) == 16
               && sizeof(struct result_hdr) == 312 && sizeof(struct res_hdr) == 72, "wire format sizes");

#define RFLAGS_VISIBLE 0xCD5ull      /* CF PF AF ZF SF DF OF */
#define RFLAGS_TF      0x100ull
#define MAX_REC        (1u << 19)    /* largest result record */
#define MAX_DIFF       (1u << 18)    /* bytes of changed memory reported per case */
#define MAX_POKE_BYTES (1u << 20)
#define MAX_STOPS      64u
#define MAX_REGIONS    64u

/* ---- parsed batch ------------------------------------------------------------------------------ */

struct blob { const uint8_t *data; uint64_t len; };
struct region { uint64_t addr, size; uint32_t prot; const struct blob *blob; uint8_t *shadow; };
struct rset { uint32_t n; struct region r[MAX_REGIONS]; int mapped; };
struct kase { const struct case_hdr *h; const uint64_t *stops; const uint8_t *pokes; };

static struct blob *g_blobs;
static struct rset *g_sets;
static struct kase *g_cases;
static struct batch_hdr g_bh;
static int g_allow_unchecked;
static const char *g_blob_dir;

static void die(const char *what)
{
    fprintf(stderr, "hworacle runner: %s\n", what);
    exit(2);
}

/* ---- raw system calls for the child (after seccomp only these three exist) ---------------------- */

static long raw_syscall3(long n, long a, long b, long c)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}

static void __attribute__((noreturn)) child_exit(int code)
{
    for (;;) raw_syscall3(SYS_exit, code, 0, 0);
}

static void child_write(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n) {
        long r = raw_syscall3(SYS_write, fd, (long)p, (long)n);
        if (r == -EINTR) continue;
        if (r <= 0) child_exit(70);
        p += r;
        n -= (size_t)r;
    }
}

/* ---- the state swap ---------------------------------------------------------------------------- */

struct gstate {
    uint64_t gpr[16], rip, rflags;
    uint32_t mxcsr;
    uint8_t xmm[16][16];
};

enum { PH_HOST = 0, PH_ENTER = 1, PH_GUEST = 2 };

static const int greg_of[16] = { REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI,
                                 REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15 };

static struct {
    volatile int phase;
    struct gstate in, out;
    /* how to run */
    uint32_t max_insns;
    int stepping;                    /* trap flag set */
    const uint64_t *stops;
    uint32_t n_stops;
    const struct rset *set;
    /* the instruction being stepped */
    uint64_t insn_addr;
    struct hwo_insn insn;
    int inspected;                   /* insn is valid (not an unchecked case) */
    /* how it ended */
    uint32_t outcome, detail, insns;
    int32_t signo, sig_code;
    uint32_t trapno, err;
    uint64_t sig_addr;
    /* the runner's own state while the guest runs */
    greg_t host_gregs[NGREG];
    uint8_t host_fp[8192];
    size_t host_fp_size;
    uint32_t mxcsr_mask;
} G;

#define FP_XSTATE_MAGIC1_ 0x46505853u
#define FX_MXCSR      24
#define FX_MXCSR_MASK 28
#define FX_XMM        160
#define FX_SW         464            /* struct _fpx_sw_bytes: magic1, extended_size, xfeatures, xstate_size */
#define XS_HEADER     512            /* xstate_bv, xcomp_bv */

static size_t fp_frame_size(const uint8_t *f)
{
    uint32_t magic, ext;
    memcpy(&magic, f + FX_SW, 4);
    memcpy(&ext, f + FX_SW + 4, 4);
    if (magic != FP_XSTATE_MAGIC1_) return 512;
    return ext;
}

static int in_stops(uint64_t a)
{
    for (uint32_t i = 0; i < G.n_stops; i++)
        if (G.stops[i] == a) return 1;
    return 0;
}

/* The executable region of the current set that holds `a`; *avail = bytes from a to its end. */
static const struct region *exec_region(const struct rset *s, uint64_t a, uint64_t *avail)
{
    for (uint32_t i = 0; i < s->n; i++) {
        const struct region *r = &s->r[i];
        if (a >= r->addr && a - r->addr < r->size && (r->prot & 5) == 5) {
            *avail = r->size - (a - r->addr);
            return r;
        }
    }
    return NULL;
}

static const struct region *region_of(const struct rset *s, uint64_t a, uint64_t n)
{
    for (uint32_t i = 0; i < s->n; i++) {
        const struct region *r = &s->r[i];
        if (a >= r->addr && a - r->addr < r->size && n <= r->size - (a - r->addr)) return r;
    }
    return NULL;
}

static void finish(greg_t *gr, uint8_t *f, uint32_t outcome, uint32_t detail)
{
    uint64_t bv = 3;
    for (int i = 0; i < 16; i++) G.out.gpr[i] = (uint64_t)gr[greg_of[i]];
    G.out.rip = (uint64_t)gr[REG_RIP];
    G.out.rflags = (uint64_t)gr[REG_EFL] & RFLAGS_VISIBLE;
    memcpy(&G.out.mxcsr, f + FX_MXCSR, 4);
    if (fp_frame_size(f) > 512) memcpy(&bv, f + XS_HEADER, 8);
    if (bv & 2) memcpy(G.out.xmm, f + FX_XMM, 256);
    else memset(G.out.xmm, 0, 256);                    /* the kernel says: SSE state is in its initial configuration */
    G.outcome = outcome;
    G.detail = detail;
    /* the runner's own state back, exactly as it was at its int3 */
    memcpy(gr, G.host_gregs, sizeof G.host_gregs);
    memcpy(f, G.host_fp, G.host_fp_size);
    G.phase = PH_HOST;
}

static void on_signal(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    greg_t *gr = uc->uc_mcontext.gregs;
    uint8_t *f = (uint8_t *)uc->uc_mcontext.fpregs;

    if (G.phase == PH_ENTER) {
        if (sig != SIGTRAP || !f) child_exit(71);
        G.host_fp_size = fp_frame_size(f);
        if (G.host_fp_size < 512 || G.host_fp_size > sizeof G.host_fp) child_exit(72);
        memcpy(G.host_gregs, gr, sizeof G.host_gregs);
        memcpy(G.host_fp, f, G.host_fp_size);
        for (int i = 0; i < 16; i++) gr[greg_of[i]] = (greg_t)G.in.gpr[i];
        gr[REG_RIP] = (greg_t)G.in.rip;
        gr[REG_EFL] = (greg_t)((G.in.rflags & RFLAGS_VISIBLE) | 0x202 | (G.stepping ? RFLAGS_TF : 0));
        /* x87: the state fninit leaves.  SSE: the guest's.  Everything else (upper YMM): initial. */
        memset(f, 0, FX_XMM);
        f[0] = 0x7F; f[1] = 0x03;
        memcpy(f + FX_MXCSR, &G.in.mxcsr, 4);
        memcpy(f + FX_MXCSR_MASK, &G.mxcsr_mask, 4);
        memcpy(f + FX_XMM, G.in.xmm, 256);
        memset(f + FX_XMM + 256, 0, FX_SW - (FX_XMM + 256));
        if (G.host_fp_size > 512) {
            uint64_t bv = 3;
            memcpy(f + XS_HEADER, &bv, 8);
        }
        G.phase = PH_GUEST;
        return;                                        /* sigreturn: the guest runs */
    }
    if (G.phase != PH_GUEST) child_exit(73);           /* a fault in the runner itself */

    uint64_t rip = (uint64_t)gr[REG_RIP];
    uint32_t trapno = (uint32_t)gr[REG_TRAPNO];
    G.signo = sig;
    G.sig_code = si->si_code;
    G.sig_addr = (uint64_t)si->si_addr;
    G.trapno = trapno;
    G.err = (uint32_t)gr[REG_ERR];
    if (trapno == 14) G.err &= 0x12;                   /* keep write and fetch; 'present' depends on the kernel's paging */

    if (sig == SIGTRAP && trapno == 1 && G.stepping) {             /* #DB: a single step completed */
        if (G.inspected && G.insn.rep_string && rip == G.insn_addr)
            return;                                    /* one iteration of a REP string instruction; go on */
        if (G.inspected && G.insn.pushf) {
            /* pushfq pushed the trap flag this runner set.  The case cannot set it, so in the
             * pushed image it is 0. */
            uint64_t sp = (uint64_t)gr[REG_RSP];
            const struct region *r = region_of(G.set, sp, G.insn.pushf);
            if (r && (r->prot & 2)) ((uint8_t *)sp)[1] &= (uint8_t)~1;
        }
        G.insns++;
        G.signo = G.sig_code = 0; G.sig_addr = 0; G.trapno = G.err = 0;
        if (in_stops(rip)) { finish(gr, f, OUT_DONE, DONE_AT_STOP); return; }
        if (G.insns >= G.max_insns) {
            int expected = G.inspected && G.insn.cls == HWO_OK && rip == G.insn_addr + G.insn.len;
            finish(gr, f, OUT_DONE, expected ? 0 : DONE_UNEXPECTED_RIP);
            return;
        }
        /* more steps: the next instruction is inspected before it runs */
        uint64_t avail;
        if (!exec_region(G.set, rip, &avail)) { finish(gr, f, OUT_DONE, DONE_UNEXPECTED_RIP); return; }
        if (G.inspected) {
            int cls = hwo_decode((const uint8_t *)rip, avail > 15 ? 15 : (unsigned)avail, &G.insn);
            if (cls == HWO_CTL && G.n_stops == 0) cls = HWO_R_CTL_NO_STOP;
            if (cls >= HWO_R_UNLISTED) { finish(gr, f, OUT_REFUSED, (uint32_t)cls); return; }
            G.insn_addr = rip;
        }
        return;
    }
    if (sig == SIGSEGV && !G.stepping && rip == G.sig_addr && (G.err & 0x10) && in_stops(rip)) {
        /* a free-running program arrived at a stop address (which is not executable) */
        G.signo = G.sig_code = 0; G.sig_addr = 0; G.trapno = G.err = 0;
        finish(gr, f, OUT_DONE, DONE_AT_STOP);
        return;
    }
    finish(gr, f, OUT_FAULT, 0);
}

static void __attribute__((noinline)) enter_guest(void)
{
    G.phase = PH_ENTER;
    __asm__ volatile("int3" : : : "memory", "cc");
}

/* ---- the child: runs a group of cases ---------------------------------------------------------- */

static uint8_t g_altstack[1 << 16] __attribute__((aligned(64)));
static uint8_t g_rec[MAX_REC + 4096];
static uint8_t g_poke_save[MAX_POKE_BYTES];

static void put_simple(int fd, uint32_t index, uint32_t outcome, uint32_t detail, uint64_t rip)
{
    struct res_hdr h;
    memset(&h, 0, sizeof h);
    h.rec_len = sizeof h - 4;
    h.index = index;
    h.outcome = outcome;
    h.detail = detail;
    h.rip = rip;
    child_write(fd, &h, sizeof h);
}

/* Changed bytes of the writable regions, lowest address first within each region, appended to
 * the record at g_rec + pos; the regions are restored from their shadows as they are scanned. */
static size_t diff_and_restore(const struct rset *s, size_t pos, uint32_t *n_runs, uint32_t *detail)
{
    const size_t limit = MAX_REC - 64;                 /* no record grows past this */
    size_t run_hdr = 0, reported = 0;                  /* the open run: header at run_hdr, run_len data bytes after it */
    uint64_t run_end = 0;
    uint32_t run_len = 0;
    int full = 0;
    *n_runs = 0;
    for (uint32_t k = 0; k < s->n; k++) {
        const struct region *r = &s->r[k];
        if (!r->shadow) continue;
        uint8_t *g = (uint8_t *)r->addr;
        const uint8_t *sh = r->shadow;
        for (uint64_t off = 0; off < r->size; off += 4096) {
            if (!memcmp(g + off, sh + off, 4096)) continue;
            for (uint64_t i = off; i < off + 4096;) {
                if (g[i] == sh[i]) { i++; continue; }
                uint64_t j = i;
                while (j < off + 4096 && g[j] != sh[j]) j++;
                uint64_t a = r->addr + i, n = j - i;
                int joins = run_len && a == run_end;   /* continues the open run across a page boundary */
                size_t at = joins ? run_hdr + 16 + run_len
                                  : (run_len ? run_hdr + 16 + ((run_len + 7u) & ~7u) : pos) + 16;
                size_t room = at + 8 <= limit ? limit - at - 8 : 0;
                if (room > MAX_DIFF - reported) room = MAX_DIFF - reported;
                if (full) n = 0;
                else if (n > room) { n = room; full = 1; }
                if (n < j - i) *detail |= DETAIL_MEM_TRUNCATED;
                if (n) {
                    if (!joins) {
                        if (run_len) pos = run_hdr + 16 + ((run_len + 7u) & ~7u);
                        run_hdr = pos;
                        run_len = 0;
                        memcpy(g_rec + run_hdr, &a, 8);
                        memset(g_rec + run_hdr + 12, 0, 4);
                        (*n_runs)++;
                    }
                    memcpy(g_rec + run_hdr + 16 + run_len, g + i, n);
                    run_len += (uint32_t)n;
                    run_end = a + n;
                    reported += n;
                    memcpy(g_rec + run_hdr + 8, &run_len, 4);
                    memset(g_rec + run_hdr + 16 + run_len, 0, 8);    /* padding to 8 */
                }
                memcpy(g + i, sh + i, j - i);
                i = j;
            }
        }
    }
    if (run_len) pos = run_hdr + 16 + ((run_len + 7u) & ~7u);
    return pos;
}

static int apply_pokes(const struct kase *k, const struct rset *s, int undo)
{
    const struct case_hdr *h = k->h;
    const uint8_t *p = k->pokes;
    size_t saved = 0;
    if (undo) {
        /* walk forward to find the offsets, then restore in reverse */
        const uint8_t *ps[256];
        size_t so[256];
        uint32_t n = h->n_pokes;
        for (uint32_t i = 0; i < n; i++) {
            struct poke_hdr ph;
            memcpy(&ph, p, sizeof ph);
            ps[i] = p; so[i] = saved;
            saved += ph.len;
            p += sizeof ph + ((ph.len + 7u) & ~7u);
        }
        for (uint32_t i = n; i-- > 0;) {
            struct poke_hdr ph;
            memcpy(&ph, ps[i], sizeof ph);
            const struct region *r = region_of(s, ph.addr, ph.len);
            memcpy((void *)ph.addr, g_poke_save + so[i], ph.len);
            memcpy(r->shadow + (ph.addr - r->addr), g_poke_save + so[i], ph.len);
        }
        return 0;
    }
    if (h->n_pokes > 256) return -1;
    for (uint32_t i = 0; i < h->n_pokes; i++) {
        struct poke_hdr ph;
        memcpy(&ph, p, sizeof ph);
        const struct region *r = region_of(s, ph.addr, ph.len);
        if (!r || !r->shadow || saved + ph.len > sizeof g_poke_save) {
            /* undo what was applied so far */
            struct case_hdr part = *h;
            struct kase pk = *k;
            part.n_pokes = i;
            pk.h = &part;
            apply_pokes(&pk, s, 1);
            return -1;
        }
        memcpy(g_poke_save + saved, (void *)ph.addr, ph.len);
        saved += ph.len;
        memcpy((void *)ph.addr, p + sizeof ph, ph.len);
        memcpy(r->shadow + (ph.addr - r->addr), p + sizeof ph, ph.len);
        p += sizeof ph + ((ph.len + 7u) & ~7u);
    }
    return 0;
}

static void run_one(int fd, uint32_t index)
{
    const struct kase *k = &g_cases[index];
    const struct case_hdr *h = k->h;
    const struct rset *s = &g_sets[h->set];
    uint64_t avail = 0;
    int unchecked = (h->flags & CASE_UNCHECKED) != 0;

    if (unchecked && !g_allow_unchecked) { put_simple(fd, index, OUT_REFUSED, HWO_R_UNCHECKED, h->rip); return; }
    if (h->mxcsr & ~G.mxcsr_mask) { put_simple(fd, index, OUT_BAD_CASE, BAD_MXCSR, h->rip); return; }
    if (apply_pokes(k, s, 0)) { put_simple(fd, index, OUT_BAD_CASE, BAD_POKE, h->rip); return; }

    const struct region *code = exec_region(s, h->rip, &avail);
    int cls = HWO_OK;
    memset(&G.insn, 0, sizeof G.insn);
    G.inspected = 0;
    if (!code) cls = HWO_R_NOT_EXEC;
    else if (h->max_insns == 0) {
        /* free-running: the whole program is checked, and it must not be able to change itself */
        if (unchecked) cls = HWO_OK;
        else if ((code->prot & 2) || h->code_len > avail) cls = HWO_R_PROGRAM;
        else cls = hwo_check_program((const uint8_t *)h->rip, h->code_len, h->rip, k->stops, h->n_stops);
    } else if (!unchecked) {
        cls = hwo_decode((const uint8_t *)h->rip, avail > 15 ? 15 : (unsigned)avail, &G.insn);
        if (cls == HWO_CTL && h->n_stops == 0) cls = HWO_R_CTL_NO_STOP;
        G.inspected = 1;
    }
    if (cls >= HWO_R_UNLISTED) {
        apply_pokes(k, s, 1);
        put_simple(fd, index, OUT_REFUSED, (uint32_t)cls, h->rip);
        return;
    }

    memcpy(G.in.gpr, h->gpr, sizeof G.in.gpr);
    G.in.rip = h->rip;
    G.in.rflags = h->rflags;
    G.in.mxcsr = h->mxcsr;
    memcpy(G.in.xmm, h->xmm, 256);
    G.max_insns = h->max_insns;
    G.stepping = h->max_insns != 0;
    G.stops = k->stops;
    G.n_stops = h->n_stops;
    G.set = s;
    G.insn_addr = h->rip;
    G.insns = 0;
    G.outcome = OUT_RUNNER_ERROR;
    G.detail = 0;
    G.signo = G.sig_code = 0; G.sig_addr = 0; G.trapno = G.err = 0;

    enter_guest();
    if (G.phase != PH_HOST) child_exit(74);

    struct res_hdr rh;
    memset(&rh, 0, sizeof rh);
    rh.index = index;
    rh.outcome = G.outcome;
    rh.detail = G.detail;
    rh.signo = G.signo; rh.sig_code = G.sig_code; rh.sig_addr = G.sig_addr; rh.trapno = G.trapno; rh.err = G.err;
    rh.rip = G.out.rip;
    rh.rflags = G.out.rflags;
    rh.mxcsr = G.out.mxcsr;
    rh.insns = G.insns;
    size_t pos = sizeof rh;
    for (int i = 0; i < 16; i++)
        if (G.out.gpr[i] != h->gpr[i]) { rh.gpr_mask |= (uint16_t)(1u << i); memcpy(g_rec + pos, &G.out.gpr[i], 8); pos += 8; }
    for (int i = 0; i < 16; i++)
        if (memcmp(G.out.xmm[i], h->xmm[i], 16)) { rh.xmm_mask |= (uint16_t)(1u << i); memcpy(g_rec + pos, G.out.xmm[i], 16); pos += 16; }
    pos = diff_and_restore(s, pos, &rh.n_mem, &rh.detail);
    apply_pokes(k, s, 1);
    rh.rec_len = (uint32_t)(pos - 4);
    memcpy(g_rec, &rh, sizeof rh);
    child_write(fd, g_rec, pos);
}

static void child_main(int fd, uint32_t first, uint32_t last)
{
    const struct case_hdr *h0 = g_cases[first].h;
    struct rlimit rl;
    stack_t ss;
    struct sigaction sa;

    /* The pipe to the parent is the only descriptor this process keeps. */
    for (int i = 0; i < 1024; i++)
        if (i != fd) close(i);
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() == 1) _exit(75);
    rl.rlim_cur = rl.rlim_max = g_bh.batch_limit_s + 5;
    setrlimit(RLIMIT_CPU, &rl);
    rl.rlim_cur = rl.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &rl);
    setrlimit(RLIMIT_FSIZE, &rl);

    memset(&ss, 0, sizeof ss);
    ss.ss_sp = g_altstack;
    ss.ss_size = sizeof g_altstack;
    if (sigaltstack(&ss, NULL)) _exit(76);
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    static const int sigs[] = { SIGTRAP, SIGSEGV, SIGBUS, SIGFPE, SIGILL };
    for (unsigned i = 0; i < 5; i++) sigaddset(&sa.sa_mask, sigs[i]);
    for (unsigned i = 0; i < 5; i++)
        if (sigaction(sigs[i], &sa, NULL)) _exit(77);

    if (syscall(SYS_arch_prctl, ARCH_SET_GS, h0->gs_base) != 0) {
        put_simple(fd, first, OUT_BAD_CASE, BAD_GS, h0->rip);
        child_exit(0);
    }
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT) != 0) {
        /* No sandbox, no guest instruction. */
        put_simple(fd, first, OUT_BAD_CASE, BAD_SANDBOX, h0->rip);
        child_exit(0);
    }
    for (uint32_t i = first; i < last; i++) run_one(fd, i);
    child_exit(0);
}

/* ---- the parent --------------------------------------------------------------------------------- */

static uint8_t *g_out;
static size_t g_out_len, g_out_cap;
static int g_out_fd = 1;             /* -1: keep in memory (the self-test) */

static void out_flush(void)
{
    size_t off = 0;
    if (g_out_fd < 0) return;
    while (off < g_out_len) {
        ssize_t r = write(g_out_fd, g_out + off, g_out_len - off);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) exit(3);                           /* nobody is listening any more */
        off += (size_t)r;
    }
    g_out_len = 0;
}

static void out_put(const void *p, size_t n)
{
    if (g_out_len + n > g_out_cap) {
        if (g_out_fd >= 0) out_flush();
        if (g_out_len + n > g_out_cap) {
            g_out_cap = (g_out_len + n) * 2 + (1 << 20);
            g_out = realloc(g_out, g_out_cap);
            if (!g_out) die("out of memory");
        }
    }
    memcpy(g_out + g_out_len, p, n);
    g_out_len += n;
}

static void out_simple(uint32_t index, uint32_t outcome, uint32_t detail)
{
    struct res_hdr h;
    memset(&h, 0, sizeof h);
    h.rec_len = sizeof h - 4;
    h.index = index;
    h.outcome = outcome;
    h.detail = detail;
    h.rip = g_cases[index].h->rip;
    out_put(&h, sizeof h);
}

static void unmap_set(struct rset *s)
{
    if (!s->mapped) return;
    for (uint32_t i = 0; i < s->n; i++) {
        munmap((void *)s->r[i].addr, s->r[i].size);
        if (s->r[i].shadow) munmap(s->r[i].shadow, s->r[i].size);
        s->r[i].shadow = NULL;
    }
    s->mapped = 0;
}

static int map_set(struct rset *s)
{
    uint64_t total = 0;
    uint32_t i;
    for (i = 0; i < s->n; i++) {
        struct region *r = &s->r[i];
        if ((r->addr & 4095) || (r->size & 4095) || !r->size || r->prot > 7 || r->size > (1ull << 30)
            || r->addr >= (1ull << 47) || r->addr + r->size > (1ull << 47) || (r->blob && r->blob->len > r->size))
            break;
        total += r->size;
        if (total > (1ull << 31)) break;
        void *p = mmap((void *)r->addr, r->size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p == MAP_FAILED) break;
        if (p != (void *)r->addr) { munmap(p, r->size); break; }     /* a kernel without MAP_FIXED_NOREPLACE */
        if (r->blob) memcpy(p, r->blob->data, r->blob->len);
        r->shadow = NULL;
        if (r->prot & 2) {
            r->shadow = mmap(NULL, r->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (r->shadow == MAP_FAILED) { r->shadow = NULL; munmap(p, r->size); break; }
            memcpy(r->shadow, p, r->size);
        }
        if (mprotect(p, r->size, (int)r->prot)) { munmap(p, r->size); if (r->shadow) munmap(r->shadow, r->size); r->shadow = NULL; break; }
    }
    if (i < s->n) {
        while (i-- > 0) {
            munmap((void *)s->r[i].addr, s->r[i].size);
            if (s->r[i].shadow) munmap(s->r[i].shadow, s->r[i].size);
            s->r[i].shadow = NULL;
        }
        return -1;
    }
    s->mapped = 1;
    return 0;
}

/* A stop address of a free-running case must fault when execution arrives: it lies in a region
 * of the case that is not executable, or it is unmapped in this process (checked by mapping a
 * page there without replacing anything, and unmapping it again). */
static int stop_is_safe(const struct rset *s, uint64_t a)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (a >= s->r[i].addr && a - s->r[i].addr < s->r[i].size) return !(s->r[i].prot & 4);
    if (a >= (1ull << 47)) return 1;                   /* not an address at all: #GP */
    void *want = (void *)(a & ~4095ull);
    void *p = mmap(want, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) return 0;
    munmap(p, 4096);
    return p == want;
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Run cases [first, last): same region set, same gs base.  A child that dies or is late costs
 * one case; a new child takes the rest. */
static void run_group(uint32_t first, uint32_t last)
{
    static uint8_t buf[MAX_REC + 4096];
    uint32_t next = first;
    while (next < last) {
        int pfd[2];
        if (pipe(pfd)) die("pipe");
        out_flush();
        pid_t pid = fork();
        if (pid < 0) die("fork");
        if (pid == 0) {
            close(pfd[0]);
            child_main(pfd[1], next, last);
        }
        close(pfd[1]);
        size_t have = 0;
        int timed_out = 0, garbage = 0;
        int64_t deadline = now_ms() + g_bh.time_limit_ms;
        for (;;) {
            struct pollfd p = { pfd[0], POLLIN, 0 };
            int64_t left = deadline - now_ms();
            int pr = left > 0 ? poll(&p, 1, (int)left) : 0;
            if (pr < 0 && errno == EINTR) continue;
            if (pr <= 0) { timed_out = 1; break; }
            ssize_t r = read(pfd[0], buf + have, sizeof buf - have);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;                         /* the child is gone */
            have += (size_t)r;
            for (;;) {                                 /* complete records */
                uint32_t len, idx;
                if (have < 8) break;
                memcpy(&len, buf, 4);
                memcpy(&idx, buf + 4, 4);
                if (len < sizeof(struct res_hdr) - 4 || len > MAX_REC || (len & 7) != 4 || idx != next) { garbage = 1; break; }
                if (have < len + 4) break;
                out_put(buf, len + 4);
                memmove(buf, buf + len + 4, have - (len + 4));
                have -= len + 4;
                next++;
                deadline = now_ms() + g_bh.time_limit_ms;
            }
            if (garbage || next >= last) break;
        }
        close(pfd[0]);
        if (timed_out || garbage) kill(pid, SIGKILL);
        int st = 0;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        if (next < last) {
            if (garbage) out_simple(next, OUT_RUNNER_ERROR, 1);
            else if (timed_out) out_simple(next, OUT_TIMEOUT, g_bh.time_limit_ms);
            else if (WIFSIGNALED(st)) out_simple(next, OUT_KILLED, (uint32_t)WTERMSIG(st));
            else out_simple(next, OUT_RUNNER_ERROR, 0x100u | (uint32_t)WEXITSTATUS(st));
            next++;
        }
    }
}

static void run_all(uint32_t n_cases)
{
    struct rset *cur = NULL;
    uint32_t i = 0;
    while (i < n_cases) {
        const struct case_hdr *h = g_cases[i].h;
        struct rset *s = &g_sets[h->set];
        if (s != cur) {
            if (cur) unmap_set(cur);
            cur = map_set(s) ? NULL : s;
        }
        if (!cur) { out_simple(i++, OUT_BAD_CASE, BAD_SET); continue; }
        if (h->max_insns == 0) {
            int ok = h->n_stops > 0;
            for (uint32_t k = 0; ok && k < h->n_stops; k++) ok = stop_is_safe(s, g_cases[i].stops[k]);
            if (!ok) { out_simple(i++, OUT_BAD_CASE, BAD_STOP); continue; }
        }
        uint32_t j = i + 1;
        while (j < n_cases && j - i < g_bh.group_size && g_cases[j].h->set == h->set
               && g_cases[j].h->gs_base == h->gs_base && g_cases[j].h->max_insns != 0 && h->max_insns != 0)
            j++;
        run_group(i, j);
        i = j;
    }
    if (cur) unmap_set(cur);
}

/* ---- self-test: is the state swap faithful on this kernel and CPU? ------------------------------ */

#define ST_CODE 0x00006A0000000000ull
#define ST_DATA 0x00006A0000010000ull

static const char *selftest(void)
{
    static const uint8_t code[] = {
        /* 00 */ 0x90,                                                   /* nop */
        /* 01 */ 0x48, 0x01, 0xD8,                                       /* add rax, rbx */
        /* 04 */ 0x66, 0x0F, 0xD4, 0xC1,                                 /* paddq xmm0, xmm1 */
        /* 08 */ 0x65, 0x48, 0x8B, 0x04, 0x25, 0x10, 0x00, 0x00, 0x00,   /* mov rax, gs:[0x10] */
        /* 11 */ 0x48, 0x89, 0x07,                                       /* mov [rdi], rax */
        /* 14 */ 0x0F, 0x05,                                             /* syscall */
        /* 16 */ 0xC5, 0xFC, 0x11, 0x07,                                 /* vmovups [rdi], ymm0  (unchecked: upper YMM must be 0) */
        /* 1a */ 0xDD, 0x3F,                                             /* fnstsw [rdi]  (unchecked: x87 must be fresh) */
    };
    static uint8_t data[64];
    struct blob blobs[2] = { { code, sizeof code }, { data, sizeof data } };
    static struct rset set;
    static struct case_hdr ch[10];
    static struct kase ks[10];
    struct batch_hdr saved_bh = g_bh;
    int saved_unchecked = g_allow_unchecked;
    const char *bad = NULL;
    uint32_t n = 0, i;

    for (i = 0; i < 64; i++) data[i] = (uint8_t)(0xA0 + i);
    memset(&set, 0, sizeof set);
    set.n = 2;
    set.r[0] = (struct region){ ST_CODE, 4096, 5, &blobs[0], NULL };
    set.r[1] = (struct region){ ST_DATA, 4096, 3, &blobs[1], NULL };
    memset(ch, 0, sizeof ch);
#define ST_CASE(off) do { ch[n].rec_len = sizeof ch[n] - 4; ch[n].max_insns = 1; ch[n].rip = ST_CODE + (off); \
                          ch[n].gs_base = ST_DATA; ch[n].mxcsr = 0x1F80; ks[n].h = &ch[n]; n++; } while (0)
    ST_CASE(0x00);                                     /* 0: nop, every bit of state patterned */
    for (i = 0; i < 16; i++) {
        ch[0].gpr[i] = 0x0123456789ABCDEFull * (i + 1) ^ 0xF0F0F0F0F0F0F0F0ull;
        for (int j = 0; j < 16; j++) ch[0].xmm[i][j] = (uint8_t)(i * 16 + j + 1);
    }
    ch[0].rflags = RFLAGS_VISIBLE;
    ch[0].mxcsr = 0xFFC0 & G.mxcsr_mask;
    ST_CASE(0x00);                                     /* 1: nop, everything zero */
    ST_CASE(0x01); ch[2].gpr[0] = 1; ch[2].gpr[3] = ~0ull;                       /* 2: 1 + -1 */
    ST_CASE(0x04); ch[3].xmm[0][0] = 0xFF; ch[3].xmm[1][0] = 1; ch[3].xmm[1][8] = 7;   /* 3 */
    ST_CASE(0x08);                                     /* 4: gs-relative load */
    ST_CASE(0x11); ch[5].gpr[0] = 0x1122334455667788ull; ch[5].gpr[7] = ST_DATA + 0x20;   /* 5: store */
    ST_CASE(0x14);                                     /* 6: must be refused */
    ST_CASE(0x16); ch[7].flags = CASE_UNCHECKED; ch[7].gpr[7] = ST_DATA;         /* 7 */
    for (int j = 0; j < 16; j++) ch[7].xmm[0][j] = (uint8_t)(0x11 + j);
    ST_CASE(0x1a); ch[8].flags = CASE_UNCHECKED; ch[8].gpr[7] = ST_DATA;         /* 8 */

    g_cases = ks; g_sets = &set; g_blobs = blobs;
    g_bh.time_limit_ms = 2000; g_bh.group_size = 64; g_bh.batch_limit_s = 30;
    g_allow_unchecked = 1;
    g_out_fd = -1; g_out_len = 0;
    run_all(n);
    g_out_fd = 1;
    g_bh = saved_bh;
    g_allow_unchecked = saved_unchecked;

    const struct res_hdr *r[10];
    size_t pos = 0;
    for (i = 0; i < n; i++) {
        if (pos + sizeof(struct res_hdr) > g_out_len) { bad = "self-test: results missing"; goto done; }
        r[i] = (const struct res_hdr *)(g_out + pos);
        if (r[i]->index != i) { bad = "self-test: results out of order"; goto done; }
        pos += r[i]->rec_len + 4;
    }
#define V64(k, off) (*(const uint64_t *)((const uint8_t *)r[k] + sizeof(struct res_hdr) + (off)))
#define EXPECT(c, what) do { if (!(c)) { bad = "self-test: " what; goto done; } } while (0)
    for (i = 0; i < 2; i++) {
        EXPECT(r[i]->outcome == OUT_DONE && r[i]->detail == 0 && r[i]->insns == 1, "a nop did not complete in one step");
        EXPECT(r[i]->rip == ST_CODE + 1, "rip after a nop");
        EXPECT(r[i]->gpr_mask == 0 && r[i]->xmm_mask == 0 && r[i]->n_mem == 0, "a nop changed registers or memory");
        EXPECT(r[i]->rflags == ch[i].rflags && r[i]->mxcsr == ch[i].mxcsr, "a nop changed rflags or mxcsr");
    }
    EXPECT(r[2]->outcome == OUT_DONE && r[2]->gpr_mask == 1 && V64(2, 0) == 0 && r[2]->rflags == 0x55 && r[2]->rip == ST_CODE + 4,
           "add rax, rbx");
    EXPECT(r[3]->outcome == OUT_DONE && r[3]->gpr_mask == 0 && r[3]->xmm_mask == 1 && V64(3, 0) == 0x100 && V64(3, 8) == 7,
           "paddq xmm0, xmm1");
    EXPECT(r[4]->outcome == OUT_DONE && r[4]->gpr_mask == 1 && V64(4, 0) == 0xB7B6B5B4B3B2B1B0ull, "gs-relative load");
    EXPECT(r[5]->outcome == OUT_DONE && r[5]->gpr_mask == 0 && r[5]->n_mem == 1 && V64(5, 0) == ST_DATA + 0x20
           && (uint32_t)V64(5, 8) == 8 && V64(5, 16) == 0x1122334455667788ull, "store to memory");
    EXPECT(r[6]->outcome == OUT_REFUSED && r[6]->detail == HWO_R_SYSCALL, "syscall was not refused");
    EXPECT(r[7]->outcome == OUT_DONE && r[7]->n_mem == 1 && V64(7, 0) == ST_DATA && (uint32_t)V64(7, 8) == 32
           && V64(7, 16) == 0x1817161514131211ull && V64(7, 32) == 0 && V64(7, 40) == 0, "upper halves of YMM are not zero");
    EXPECT(r[8]->outcome == OUT_DONE && r[8]->n_mem == 1 && V64(8, 0) == ST_DATA && (uint32_t)V64(8, 8) == 2
           && (V64(8, 16) & 0xFFFF) == 0, "x87 status word is not fresh");
done:
    g_out_len = 0;
    return bad;
}

/* ---- reading the batch --------------------------------------------------------------------------- */

static uint8_t *read_all(int fd, size_t *len)
{
    size_t cap = 1 << 20, n = 0;
    uint8_t *b = malloc(cap);
    if (!b) die("out of memory");
    for (;;) {
        if (n == cap) {
            if (cap >= ((size_t)1 << 30)) die("batch larger than 1 GiB");
            cap *= 2;
            b = realloc(b, cap);
            if (!b) die("out of memory");
        }
        ssize_t r = read(fd, b + n, cap - n);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) die("read error on stdin");
        if (r == 0) break;
        n += (size_t)r;
    }
    *len = n;
    return b;
}

static uint8_t *load_blob_file(const uint8_t sha[32], uint64_t len)
{
    char path[4096], hex[65];
    uint8_t d[32], *b;
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", sha[i]);
    if (!g_blob_dir || snprintf(path, sizeof path, "%s/%s", g_blob_dir, hex) >= (int)sizeof path) return NULL;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    b = malloc(len ? len : 1);
    size_t n = 0;
    while (b && n < len) {
        ssize_t r = read(fd, b + n, len - n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        n += (size_t)r;
    }
    char extra;
    int longer = b && n == len && read(fd, &extra, 1) == 1;
    close(fd);
    if (!b || n != len || longer) { free(b); return NULL; }
    hwo_sha256(b, len, d);
    if (memcmp(d, sha, 32)) { free(b); return NULL; }  /* truncated or damaged: as good as missing */
    return b;
}

static void fill_header(struct result_hdr *rh, int aslr_off)
{
    unsigned a, b, c, d;
    struct utsname un;
    memset(rh, 0, sizeof *rh);
    rh->magic = MAGIC_RESULT;
    rh->version = HWO_FORMAT_VERSION;
    rh->answers_rev = HWO_ANSWERS_REV;
    __cpuid(1, a, b, c, d);
    rh->cpu_signature = a;
    if (c & (1u << 27)) {                              /* OSXSAVE */
        unsigned lo, hi;
        __asm__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        rh->xcr0 = (uint64_t)hi << 32 | lo;
    }
    for (unsigned k = 0; k < 3; k++) {
        __cpuid(0x80000002u + k, a, b, c, d);
        memcpy(rh->brand + 16 * k, &a, 4); memcpy(rh->brand + 16 * k + 4, &b, 4);
        memcpy(rh->brand + 16 * k + 8, &c, 4); memcpy(rh->brand + 16 * k + 12, &d, 4);
    }
    rh->brand[47] = 0;
    if (!uname(&un)) snprintf(rh->kernel, sizeof rh->kernel, "%.15s %.40s", un.sysname, un.release);
    snprintf(rh->build, sizeof rh->build, "%s", HWO_BUILD_ID);
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "microcode", 9)) {
                char *p = strchr(line, ':');
                if (p) rh->microcode = (uint32_t)strtoul(p + 1, NULL, 0);
                break;
            }
        fclose(f);
    }
    if (aslr_off) rh->flags |= RH_NO_ASLR;
    if (getauxval(AT_HWCAP2) & 2) rh->flags |= RH_FSGSBASE;
}

static void __attribute__((noreturn)) fail_batch(struct result_hdr *rh, uint32_t status, const char *msg,
                                                 const void *extra, uint32_t n_extra)
{
    uint32_t end[2] = { MAGIC_END_OUT, n_extra };
    rh->status = status;
    rh->n_results = n_extra;
    snprintf(rh->message, sizeof rh->message, "%s", msg);
    g_out_len = 0;
    out_put(rh, sizeof *rh);
    if (n_extra) out_put(extra, 32 * (size_t)n_extra);
    out_put(end, sizeof end);
    out_flush();
    exit(status == BATCH_OK ? 0 : status == BATCH_MISSING_BLOBS ? 4 : 5);
}

int main(int argc, char **argv)
{
    int no_reexec = 0, selftest_only = 0;
    struct result_hdr rh;
    size_t len, pos;
    uint8_t *buf;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--blobs") && i + 1 < argc) g_blob_dir = argv[++i];
        else if (!strcmp(argv[i], "--allow-unchecked")) g_allow_unchecked = 1;
        else if (!strcmp(argv[i], "--no-reexec")) no_reexec = 1;
        else if (!strcmp(argv[i], "--selftest")) selftest_only = 1;
        else die("usage: runner [--blobs DIR] [--allow-unchecked] [--selftest] < batch > results");
    }
    /* A fixed address-space layout: the answers must not depend on where the kernel happened to
     * put this program.  Switch randomisation off and start again. */
    int pers = personality(0xFFFFFFFFu);
    if (pers != -1 && !(pers & ADDR_NO_RANDOMIZE) && !no_reexec) {
        if (personality((unsigned long)pers | ADDR_NO_RANDOMIZE) != -1) {
            char **av = calloc((size_t)argc + 2, sizeof *av);
            if (av) {
                memcpy(av, argv, (size_t)argc * sizeof *av);
                av[argc] = "--no-reexec";
                execv("/proc/self/exe", av);
            }
        }
        pers = personality(0xFFFFFFFFu);
    }
    signal(SIGPIPE, SIG_DFL);
    if (nice(10) == -1) { /* priority is a courtesy, not a requirement */ }

    fill_header(&rh, pers != -1 && (pers & ADDR_NO_RANDOMIZE));
    {   /* which MXCSR bits exist on this CPU */
        static uint8_t fx[512] __attribute__((aligned(64)));
        __asm__ volatile("fxsave64 %0" : "=m"(fx));
        memcpy(&G.mxcsr_mask, fx + FX_MXCSR_MASK, 4);
        if (!G.mxcsr_mask) G.mxcsr_mask = 0xFFBF;
    }
    alarm(60);
    const char *bad = selftest();
    if (bad) fail_batch(&rh, BATCH_SELFTEST_FAILED, bad, NULL, 0);
    if (selftest_only) fail_batch(&rh, BATCH_OK, "self-test passed", NULL, 0);

    buf = read_all(0, &len);
    if (len < sizeof g_bh) fail_batch(&rh, BATCH_BAD_INPUT, "input shorter than a batch header", NULL, 0);
    memcpy(&g_bh, buf, sizeof g_bh);
    if (g_bh.magic != MAGIC_BATCH) fail_batch(&rh, BATCH_BAD_INPUT, "bad magic", NULL, 0);
    if (g_bh.version != HWO_FORMAT_VERSION) fail_batch(&rh, BATCH_BAD_INPUT, "format version not supported by this runner", NULL, 0);
    if (!g_bh.time_limit_ms) g_bh.time_limit_ms = 2000;
    if (g_bh.time_limit_ms > 60000) g_bh.time_limit_ms = 60000;
    if (!g_bh.group_size) g_bh.group_size = 64;
    if (!g_bh.batch_limit_s) g_bh.batch_limit_s = 120;
    if (g_bh.batch_limit_s > 600) g_bh.batch_limit_s = 600;
    if (g_bh.n_blobs > (1u << 16) || g_bh.n_sets > (1u << 16) || g_bh.n_cases > (1u << 24))
        fail_batch(&rh, BATCH_BAD_INPUT, "counts out of range", NULL, 0);
    alarm(g_bh.batch_limit_s);
    pos = sizeof g_bh;
#define INPUT(cond) do { if (!(cond)) fail_batch(&rh, BATCH_BAD_INPUT, "malformed batch (" #cond ")", NULL, 0); } while (0)

    g_blobs = calloc(g_bh.n_blobs + 1, sizeof *g_blobs);
    uint8_t *missing = calloc(g_bh.n_blobs + 1, 32);
    uint32_t n_missing = 0;
    INPUT(g_blobs && missing);
    for (uint32_t i = 0; i < g_bh.n_blobs; i++) {
        struct blob_hdr bh;
        uint8_t d[32];
        INPUT(pos + sizeof bh <= len);
        memcpy(&bh, buf + pos, sizeof bh);
        pos += sizeof bh;
        INPUT(bh.len <= (1ull << 30) && bh.where <= 1);
        g_blobs[i].len = bh.len;
        if (bh.where == 0) {
            uint64_t padded = (bh.len + 7) & ~7ull;
            INPUT(padded <= len - pos);
            g_blobs[i].data = buf + pos;
            pos += padded;
            hwo_sha256(g_blobs[i].data, bh.len, d);
            INPUT(!memcmp(d, bh.sha256, 32));
        } else {
            g_blobs[i].data = load_blob_file(bh.sha256, bh.len);
            if (!g_blobs[i].data) memcpy(missing + 32 * n_missing++, bh.sha256, 32);
        }
    }
    if (n_missing) fail_batch(&rh, BATCH_MISSING_BLOBS, "memory blobs missing or damaged in the blob directory", missing, n_missing);

    g_sets = calloc(g_bh.n_sets + 1, sizeof *g_sets);
    INPUT(g_sets != NULL);
    for (uint32_t i = 0; i < g_bh.n_sets; i++) {
        uint32_t n;
        INPUT(pos + 8 <= len);
        memcpy(&n, buf + pos, 4);
        pos += 8;
        INPUT(n <= MAX_REGIONS && pos + (size_t)n * sizeof(struct region_rec) <= len);
        g_sets[i].n = n;
        for (uint32_t k = 0; k < n; k++) {
            struct region_rec rr;
            memcpy(&rr, buf + pos, sizeof rr);
            pos += sizeof rr;
            INPUT(rr.blob == 0xFFFFFFFFu || rr.blob < g_bh.n_blobs);
            g_sets[i].r[k].addr = rr.addr;
            g_sets[i].r[k].size = rr.size;
            g_sets[i].r[k].prot = rr.prot;
            g_sets[i].r[k].blob = rr.blob == 0xFFFFFFFFu ? NULL : &g_blobs[rr.blob];
        }
    }
    {
        uint32_t m;
        INPUT(pos + 8 <= len);
        memcpy(&m, buf + pos, 4);
        INPUT(m == MAGIC_CASES);
        pos += 8;
    }
    g_cases = calloc(g_bh.n_cases + 1, sizeof *g_cases);
    INPUT(g_cases != NULL);
    for (uint32_t i = 0; i < g_bh.n_cases; i++) {
        const struct case_hdr *h = (const struct case_hdr *)(buf + pos);
        INPUT(pos + sizeof *h <= len && (pos & 7) == 0);
        INPUT(h->rec_len >= sizeof *h - 4 && (h->rec_len & 7) == 4 && h->rec_len <= len - pos - 4);
        INPUT(h->set < g_bh.n_sets && h->n_stops <= MAX_STOPS && h->n_pokes <= 256 && (h->flags & ~CASE_UNCHECKED) == 0);
        size_t end = pos + 4 + h->rec_len, q = pos + sizeof *h + 8 * (size_t)h->n_stops;
        INPUT(q <= end);
        g_cases[i].h = h;
        g_cases[i].stops = (const uint64_t *)(buf + pos + sizeof *h);
        g_cases[i].pokes = buf + q;
        for (uint32_t k = 0; k < h->n_pokes; k++) {
            struct poke_hdr ph;
            INPUT(q + sizeof ph <= end);
            memcpy(&ph, buf + q, sizeof ph);
            INPUT(ph.len <= MAX_POKE_BYTES && ((ph.len + 7u) & ~7u) <= end - q - sizeof ph);
            q += sizeof ph + ((ph.len + 7u) & ~7u);
        }
        INPUT(q == end);
        pos = end;
    }
    {
        uint32_t m;
        INPUT(pos + 8 == len);
        memcpy(&m, buf + pos, 4);
        INPUT(m == MAGIC_END_IN);
    }

    rh.status = BATCH_OK;
    rh.n_results = g_bh.n_cases;
    out_put(&rh, sizeof rh);
    run_all(g_bh.n_cases);
    uint32_t end[2] = { MAGIC_END_OUT, g_bh.n_cases };
    out_put(end, sizeof end);
    out_flush();
    return 0;
}
#endif /* HWO_CHECK_TOOL */
