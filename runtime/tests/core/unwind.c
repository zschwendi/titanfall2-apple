/* The unwinder (rt/unwind.h; runtime/core/unwind.c).
 *
 *   core/unwind          hand-made unwind data in the test image t_lift_a.dll: every unwind
 *                        code, the prologue rule at every instruction boundary, the epilogue
 *                        forms, chaining, version 2, the limits, lookups, capture, the
 *                        return-to-host stub, backtraces over a hand-made stack
 *   core/unwind_corpus   compiler-made unwind data: a capture three guest calls deep inside
 *                        the lifted corpus (tests/corpus/calls.c, callback_walk), walked back
 *                        to the test's own call; the guest stack lines of the fatal reports,
 *                        in child processes
 *
 * HOW A HAND-MADE CASE WORKS.  The "stack" is a committed guest region in which the word at
 * address a holds pat(a), so a register restored from the right slot can only have one
 * value.  Every register starts as a canary; a case names the slots its registers must come
 * from and where the return address is, and unwind_case checks every register, every
 * context pointer, Rip, Rsp and the establisher frame.
 */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/test_child.h"
#include "rt/module.h"
#include "rt/thread.h"
#include "rt/unwind.h"
#include "rt/vm.h"
#include "rt/../../core/report.h"       /* rt_report_backtrace */

#define CORPUS_REL "bin/x64_retail/corpus_O1.dll"

/* ---- the test image ----------------------------------------------------------------------------- */

#define UC(off, op, info)  (uint8_t)(off), (uint8_t)((op) | ((info) << 4))
#define U16(v)             (uint8_t)(v), (uint8_t)((uint32_t)(v) >> 8)
#define U32(v)             U16(v), U16((uint32_t)(v) >> 16)
#define HDR(ver, flags, prolog, count, freg, foff) \
    (uint8_t)((ver) | ((flags) << 3)), (uint8_t)(prolog), (uint8_t)(count), (uint8_t)((freg) | ((foff) << 4))

enum { RBX = 3, RBP = 5, RSI = 6, RDI = 7, R12 = 12, R13 = 13, XMM = 16 };
enum {
    F1 = 0x1100, F1_END = 0x1180, F1_EPI = 0x1170,      /* push rbx; push rsi; push r12; sub rsp, 0x28 */
    F2 = 0x1200, F2_END = 0x1300, F2_EPI = 0x12F0,      /* a frame pointer, a large frame, every save code */
    F3 = 0x1300, F3_END = 0x1340, F3_EPI = 0x1330,      /* frame register r13 */
    F4 = 0x1340, F4_END = 0x1380, F4_EPI = 0x1360,      /* UWOP_ALLOC_LARGE with a 32-bit size */
    PA = 0x1400, PA_END = 0x1440,                       /* a primary entry: push rbx; sub rsp, 0x20 */
    CH = 0x1500, CH_END = 0x1540,                       /* chained to PA; saves rsi at +0x10 */
    C2 = 0x1540, C2_END = 0x1580,                       /* chained to CH; saves rdi at +0x18 */
    M0 = 0x1580, M0_END = 0x15A0,                       /* machine frame, then an 8-byte allocation */
    M1 = 0x15A0, M1_END = 0x15C0,                       /* machine frame with an error code */
    FT = 0x1600, FT_END = 0x1700,                       /* sub rsp, 0x28; holds the epilogue endings */
    V2 = 0x1700, V2_END = 0x1740,                       /* version 2: push rbx; push r12; sub rsp, 0x20 */
    VP = 0x1740, VP_END = 0x1780,                       /* version 2, one epilogue slot and a padding slot */
    HF = 0x1780, HF_END = 0x17C0, HF_HANDLER = 0x1F00,  /* has an exception handler */
    LEAF = 0x1800,                                      /* no entry */
    BV = 0x1880, BO = 0x18A0, B6 = 0x18C0, BP = 0x18E0, BL = 0x1900, BI = 0x1920, BA = 0x1940,   /* the limits */
    U_F1 = 0x3000, U_F2 = 0x3020, U_F3 = 0x3040, U_F4 = 0x3050, U_PA = 0x3070, U_CH = 0x3080, U_C2 = 0x30A0,
    U_M0 = 0x30C0, U_M1 = 0x30C8, U_V2 = 0x30D0, U_HF = 0x30E0, U_FT = 0x30F0,
    U_BV = 0x3100, U_BO = 0x3108, U_B6 = 0x3110, U_BP = 0x3118, U_BL = 0x3120, U_BA = 0x3130,
    U_VP = 0x3180
};

static void code(RtTestPe *pe, uint32_t rva, const char *hex) {
    uint8_t b[32];
    unsigned n = 0, v;
    for (; sscanf(hex, "%2x", &v) == 1; hex += hex[2] == ' ' ? 3 : 2) b[n++] = (uint8_t)v;
    rt_test_pe_put(pe, rva, b, n);
}

/* `pop rbx; jmp rel32` at `rva`, to `target`. */
static void pop_jmp32(RtTestPe *pe, uint32_t rva, uint32_t target) {
    uint8_t b[6] = { 0x5B, 0xE9 };
    uint32_t rel = target - (rva + 6);
    memcpy(b + 2, &rel, 4);
    rt_test_pe_put(pe, rva, b, 6);
}

#define PDATA(begin, end, urva, ...) \
    do { const uint8_t u_[] = { __VA_ARGS__ }; rt_test_pe_pdata(pe, (begin), (end), (urva), u_, sizeof u_); } while (0)

static void build_image(void) {
    RtTestPe *pe = rt_test_pe_new();
    PDATA(F1, F1_END, U_F1, HDR(1, 0, 8, 4, 0, 0), UC(8, UWOP_ALLOC_SMALL, 4), UC(4, UWOP_PUSH_NONVOL, R12),
          UC(2, UWOP_PUSH_NONVOL, RSI), UC(1, UWOP_PUSH_NONVOL, RBX));
    code(pe, F1, "53 56 41 54 48 83 EC 28");
    code(pe, F1_EPI, "48 83 C4 28 41 5C 5E 5B C3");     /* add rsp, 0x28; pop r12; pop rsi; pop rbx; ret */

    /* push rbp; sub rsp, 0x1000; lea rbp, [rsp+0x20]; mov [rsp+0x38], rsi; movaps [rsp+0x40], xmm6;
     * mov [rsp+0x800], rdi; movaps [rsp+0x900], xmm15.  The offsets after each: 1 8 13 18 23 31 40. */
    PDATA(F2, F2_END, U_F2, HDR(1, 0, 40, 14, RBP, 2),
          UC(40, UWOP_SAVE_XMM128_FAR, 15), U32(0x900), UC(31, UWOP_SAVE_NONVOL_FAR, RDI), U32(0x800),
          UC(23, UWOP_SAVE_XMM128, 6), U16(0x40 / 16), UC(18, UWOP_SAVE_NONVOL, RSI), U16(0x38 / 8),
          UC(13, UWOP_SET_FPREG, 0), UC(8, UWOP_ALLOC_LARGE, 0), U16(0x1000 / 8), UC(1, UWOP_PUSH_NONVOL, RBP));
    code(pe, F2_EPI, "48 8D A5 E0 0F 00 00 5D C3");     /* lea rsp, [rbp+0xFE0]; pop rbp; ret */

    PDATA(F3, F3_END, U_F3, HDR(1, 0, 11, 3, R13, 1), UC(11, UWOP_SET_FPREG, 0), UC(6, UWOP_ALLOC_SMALL, 7),
          UC(2, UWOP_PUSH_NONVOL, R13), 0, 0);
    code(pe, F3_EPI, "49 8D 65 30 41 5D C3");           /* lea rsp, [r13+0x30]; pop r13; ret */
    code(pe, F3 + 0x38, "49 8D 65 38 C3");              /* lea rsp, [r13+0x38]; ret: r13 is not popped */

    PDATA(F4, F4_END, U_F4, HDR(1, 0, 7, 3, 0, 0), UC(7, UWOP_ALLOC_LARGE, 1), U32(0x2000), 0, 0);
    code(pe, F4_EPI, "48 81 C4 00 20 00 00 C3");        /* add rsp, 0x2000; ret */
    code(pe, F4 + 0x30, "48 81 C4 00 20 00 00 5B C3");  /* add rsp, 0x2000; pop rbx; ret */

    PDATA(PA, PA_END, U_PA, HDR(1, 0, 5, 2, 0, 0), UC(5, UWOP_ALLOC_SMALL, 3), UC(1, UWOP_PUSH_NONVOL, RBX));
    PDATA(CH, CH_END, U_CH, HDR(1, UNW_FLAG_CHAININFO, 5, 2, 0, 0), UC(5, UWOP_SAVE_NONVOL, RSI), U16(0x10 / 8),
          U32(PA), U32(PA_END), U32(U_PA));
    PDATA(C2, C2_END, U_C2, HDR(1, UNW_FLAG_CHAININFO, 0, 2, 0, 0), UC(0, UWOP_SAVE_NONVOL, RDI), U16(0x18 / 8),
          U32(CH), U32(CH_END), U32(U_CH));
    pop_jmp32(pe, CH + 0x20, PA + 0x20);                /* into another entry of the same function: no tail call */
    pop_jmp32(pe, CH + 0x28, PA);                       /* to the function's start: a tail call */

    PDATA(M0, M0_END, U_M0, HDR(1, 0, 4, 2, 0, 0), UC(4, UWOP_ALLOC_SMALL, 0), UC(0, UWOP_PUSH_MACHFRAME, 0));
    PDATA(M1, M1_END, U_M1, HDR(1, 0, 0, 1, 0, 0), UC(0, UWOP_PUSH_MACHFRAME, 1), 0, 0);

    PDATA(FT, FT_END, U_FT, HDR(1, 0, 4, 1, 0, 0), UC(4, UWOP_ALLOC_SMALL, 4), 0, 0);
    code(pe, FT + 0x10, "5B EB ED");                    /* pop rbx; jmp to FT itself */
    code(pe, FT + 0x20, "5B C3");                       /* ret */
    code(pe, FT + 0x30, "5B C2 08 00");                 /* ret 8 */
    code(pe, FT + 0x40, "5B F3 C3");                    /* rep ret */
    code(pe, FT + 0x50, "5B F2 C3");                    /* a prefixed ret */
    pop_jmp32(pe, FT + 0x60, F1);                       /* jmp to another function */
    pop_jmp32(pe, FT + 0x70, FT + 0x80);                /* jmp inside FT */
    code(pe, FT + 0x80, "5B EB 10");                    /* jmp rel8 inside FT */
    code(pe, FT + 0x90, "5B FF 25 00 00 00 00");        /* jmp [rip+0] */
    code(pe, FT + 0xA0, "5B 48 FF E0");                 /* REX.W jmp rax */
    code(pe, FT + 0xB0, "5B FF E0");                    /* jmp rax without the prefix */
    pop_jmp32(pe, FT + 0xC0, 0x1F00);                   /* jmp to where no entry is */
    code(pe, FT + 0xD0, "41 5C 5B C3");                 /* pop r12; pop rbx; ret */
    code(pe, FT + 0xE0, "48 83 C4 28 5B C3");           /* add rsp, 0x28; pop rbx; ret */

    /* Version 2.  Epilogues of 4 bytes (pop r12; pop rbx; ret): one ends the function, one starts 0x20
     * before its end.  The bytes at V2 + 0x10 would be an epilogue to the version 1 decoder. */
    PDATA(V2, V2_END, U_V2, HDR(2, 0, 7, 5, 0, 0), UC(4, UWOP_EPILOG, 1), UC(0x20, UWOP_EPILOG, 0),
          UC(7, UWOP_ALLOC_SMALL, 3), UC(3, UWOP_PUSH_NONVOL, R12), UC(1, UWOP_PUSH_NONVOL, RBX), 0, 0);
    code(pe, V2 + 0x10, "5B C3");
    /* The same function with its one epilogue at the end: a single UWOP_EPILOG slot, and a
     * padding slot of zeros to keep the count of epilogue slots even.  The walk over the codes
     * takes the two as one code; read alone, the zero slot would be "push rax". */
    PDATA(VP, VP_END, U_VP, HDR(2, 0, 7, 5, 0, 0), UC(4, UWOP_EPILOG, 1), 0, 0,
          UC(7, UWOP_ALLOC_SMALL, 3), UC(3, UWOP_PUSH_NONVOL, R12), UC(1, UWOP_PUSH_NONVOL, RBX), 0, 0);

    PDATA(HF, HF_END, U_HF, HDR(1, UNW_FLAG_EHANDLER, 4, 1, 0, 0), UC(4, UWOP_ALLOC_SMALL, 4), 0, 0,
          U32(HF_HANDLER), 'D', 'A', 'T', 'A');

    PDATA(BV, BV + 0x20, U_BV, HDR(3, 0, 0, 0, 0, 0));
    PDATA(BO, BO + 0x20, U_BO, HDR(1, 0, 0, 1, 0, 0), UC(0, 11, 0), 0, 0);
    PDATA(B6, B6 + 0x20, U_B6, HDR(1, 0, 0, 1, 0, 0), UC(0, UWOP_EPILOG, 0), 0, 0);
    PDATA(BP, BP + 0x20, U_BP, HDR(1, 0, 0, 1, 0, 0), UC(0, UWOP_SAVE_NONVOL, RBX), 0, 0);
    PDATA(BL, BL + 0x20, U_BL, HDR(1, UNW_FLAG_CHAININFO, 0, 0, 0, 0), U32(BL), U32(BL + 0x20), U32(U_BL));
    PDATA(BA, BA + 0x20, U_BA, HDR(1, 0, 0, 2, 0, 0), UC(0, UWOP_ALLOC_LARGE, 2), U16(1));
    rt_test_pe_pdata(pe, BI, BI + 0x20, U_PA + 1, NULL, 0);     /* bit 0 of UnwindData */
    rt_test_pe_write(pe, "t_lift_a.dll");
}

/* ---- the hand-made stack ------------------------------------------------------------------------ */

#define STACK_BYTES 0x8000u
#define RET_MARK    UINT64_C(0x00000ABCDEF01230)
#define NO_FRAME    UINT64_MAX
#define CANARY(r)   (UINT64_C(0xC0DE00000000) + (r))

static uint64_t B;              /* base of t_lift_a.dll */
static uint64_t g;              /* the stack region: STACK_BYTES committed */
static uint64_t ur;             /* a reserved region: not readable */

static uint64_t pat(uint64_t a) { return a * UINT64_C(0x9E3779B97F4A7C15) | 1; }
static void fill_stack(void) { for (uint64_t a = g; a < g + STACK_BYTES; a += 8) ST64(a, pat(a)); }

typedef struct { int reg; uint64_t off; } Saved;        /* register (XMM + n for xmm n) restored from [S + off] */
#define SV(...)  (const Saved[]){ __VA_ARGS__ }, (unsigned)(sizeof((const Saved[]){ __VA_ARGS__ }) / sizeof(Saved))
#define NONE     NULL, 0
#define CK(cond) rt_test_check(!!(cond), __FILE__, line, #cond, 0, 0, 0, 0.0, 0.0)

/* Unwind one frame from `pc` with Rsp = `rsp` and register `preg` (if >= 0) = `pval`.  The
 * saved registers and the return address are at offsets from `S`. */
static void unwind_case(int line, uint64_t pc, uint64_t rsp, uint64_t S, int preg, uint64_t pval,
                        const Saved *sv, unsigned nsv, uint64_t ret_off, uint64_t want_frame) {
    CONTEXT c;
    KNONVOLATILE_CONTEXT_POINTERS p;
    uint64_t base = 0, frame = 0, hdata = 0x77;
    fill_stack();
    ST64(S + ret_off, RET_MARK);
    memset(&c, 0, sizeof c);
    memset(&p, 0, sizeof p);
    for (unsigned r = 0; r < 16; r++) {
        (&c.Rax)[r] = CANARY(r);
        memset(c.FltSave + RT_CONTEXT_FLTSAVE_XMM0 + 16 * r, 0xE0 + (int)r, 16);
    }
    if (preg >= 0) (&c.Rax)[preg] = pval;
    c.Rsp = rsp;
    c.Rip = pc;
    const RUNTIME_FUNCTION *fn = rt_unwind_lookup(pc, &base);
    CK(fn != NULL && base == B);
    if (!fn) return;
    GUESTFN handler = rt_unwind_step(UNW_FLAG_NHANDLER, base, pc, fn, &c, &hdata, &frame, &p);
    CK(handler == 0 && hdata == 0x77);
    CK(c.Rip == RET_MARK);
    CK(c.Rsp == S + ret_off + 8);
    if (want_frame != NO_FRAME) CK(frame == want_frame);
    for (unsigned r = 0; r < 32; r++) {
        const Saved *s = NULL;
        for (unsigned k = 0; k < nsv; k++) if (sv[k].reg == (int)r) s = &sv[k];
        if (r < 16) {
            if (r == 4) continue;
            uint64_t want = s ? pat(S + s->off) : (int)r == preg ? pval : CANARY(r);
            CK((&c.Rax)[r] == want);
            CK((uint64_t)(uintptr_t)p.IntegerContext[r] == (s ? S + s->off : 0));
        } else {
            uint8_t want[16];
            if (s) memcpy(want, (const void *)(uintptr_t)(S + s->off), 16);
            else memset(want, 0xE0 + (int)(r - 16), 16);
            CK(memcmp(c.FltSave + RT_CONTEXT_FLTSAVE_XMM0 + 16 * (r - 16), want, 16) == 0);
            CK((uint64_t)(uintptr_t)p.FloatingContext[r - 16] == (s ? S + s->off : 0));
        }
    }
}
#define CASE(pc, rsp, S, preg, pval, saved, ret_off, frame) \
    unwind_case(__LINE__, B + (pc), (rsp), (S), (preg), (pval), saved, (ret_off), (frame))

/* Lookup and one step at `rva` with a readable stack: for the unwind data that is fatal. */
static void step_at(uint32_t rva) {
    CONTEXT c;
    uint64_t base = 0, frame = 0, hdata = 0;
    memset(&c, 0, sizeof c);
    c.Rsp = g + 0x2000;
    const RUNTIME_FUNCTION *fn = rt_unwind_lookup(B + rva, &base);
    if (fn) rt_unwind_step(UNW_FLAG_NHANDLER, base, B + rva, fn, &c, &hdata, &frame, NULL);
}

static Ctx hand_ctx(uint64_t rsp) {
    Ctx x;
    memset(&x, 0, sizeof x);            /* host NULL: no thread behind it */
    for (unsigned r = 0; r < 16; r++) x.r[r] = CANARY(r);
    x.r[4] = rsp;
    return x;
}

TEST_SETUP(core, unwind) { build_image(); }
TEST(core, unwind)
{
    Ctx *ctx = T_CTX();
    RtModule *m = rt_module_load_a(ctx, "t_lift_a.dll", 0);
    T_REQUIRE(m != NULL);
    B = m->base;
    g = rt_vm_alloc(0, STACK_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ur = rt_vm_alloc(0, 0x10000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE);
    T_REQUIRE(g != 0 && ur != 0);
    const uint64_t S = g + 0x2000;
    const uint64_t stub = rt_return_to_host_address();
    RtModule *ntdll = rt_module_find("ntdll.dll"), *k32 = rt_module_find("kernel32.dll");
    T_REQUIRE(ntdll && k32 && rt_module_from_address(stub) == ntdll);

    T_CASE("lookup");
    {
        uint64_t base = 1;
        const RUNTIME_FUNCTION *a = rt_unwind_lookup(B + F1, &base), *b = rt_unwind_lookup(B + F1_END - 1, NULL);
        T_TRUE(a != NULL && a == b && base == B);
        T_TRUE(a && a->BeginAddress == F1 && a->EndAddress == F1_END && a->UnwindData == U_F1);
        T_TRUE((uint64_t)(uintptr_t)a - B >= 0x8000 && (uint64_t)(uintptr_t)a - B < 0x10000);    /* in the mapped image */
        base = 1;
        T_TRUE(rt_unwind_lookup(B + F1_END, &base) == NULL && base == B);      /* the gap after F1 */
        T_TRUE(rt_unwind_lookup(B + F1 - 1, NULL) == NULL);                    /* below the first row */
        T_TRUE(rt_unwind_lookup(B + LEAF, &base) == NULL && base == B);
        T_TRUE(rt_unwind_lookup(B + 0xFFFF, NULL) == NULL);                    /* above the last row */
        const RUNTIME_FUNCTION *c = rt_unwind_lookup(B + CH + 0x10, NULL);     /* a chained row is returned as it is */
        T_TRUE(c && c->BeginAddress == CH && c->UnwindData == U_CH);
        const RUNTIME_FUNCTION *prev = NULL;                                   /* every row is found, in table order */
        static const uint32_t all[] = { F1, F2, F3, F4, PA, CH, C2, M0, M1, FT, V2, VP, HF, BV, BO, B6, BP, BL, BA };
        for (unsigned i = 0; i < sizeof all / sizeof *all; i++) {
            const RUNTIME_FUNCTION *r = rt_unwind_lookup(B + all[i], NULL);
            T_TRUE(r && r->BeginAddress == all[i] && (!prev || r > prev));
            prev = r;
        }
        base = 1;
        T_TRUE(rt_unwind_lookup(0, &base) == NULL && base == 0);
        base = 1;
        T_TRUE(rt_unwind_lookup(g, &base) == NULL && base == 0);               /* the VM arena: no module */
        RtModule *absent = rt_module_find("t_lift_b.dll");
        base = 1;
        T_TRUE(absent && rt_unwind_lookup(absent->base + 0x1000, &base) == NULL && base == 0);   /* not loaded */
        base = 1;
        T_TRUE(rt_unwind_lookup(k32->base + RT_NATIVE_SHIM_STUBS, &base) == NULL && base == k32->base);
        T_TRAPS(rt_unwind_lookup(B + BI + 8, NULL), "is an indirect one");
    }

    T_CASE("leaf");
    {
        CONTEXT c;
        uint64_t frame = 0;
        memset(&c, 0, sizeof c);
        ST64(S, RET_MARK);
        c.Rsp = S;
        c.Rbx = 5;
        T_EQ(rt_unwind_step(UNW_FLAG_NHANDLER, B, B + LEAF, NULL, &c, NULL, &frame, NULL), 0);
        T_TRUE(c.Rip == RET_MARK && c.Rsp == S + 8 && frame == S && c.Rbx == 5);
    }

    T_CASE("the prologue rule at every instruction boundary, pushes and a small allocation");
    CASE(F1 + 0, S, S, -1, 0, NONE, 0, S);
    CASE(F1 + 1, S, S, -1, 0, SV({ RBX, 0 }), 8, S);
    CASE(F1 + 2, S, S, -1, 0, SV({ RSI, 0 }, { RBX, 8 }), 0x10, S);
    CASE(F1 + 3, S, S, -1, 0, SV({ RSI, 0 }, { RBX, 8 }), 0x10, S);           /* inside `push r12`: not pushed yet */
    CASE(F1 + 4, S, S, -1, 0, SV({ R12, 0 }, { RSI, 8 }, { RBX, 0x10 }), 0x18, S);
    CASE(F1 + 8, S, S, -1, 0, SV({ R12, 0x28 }, { RSI, 0x30 }, { RBX, 0x38 }), 0x40, S);
    CASE(F1 + 0x20, S, S, -1, 0, SV({ R12, 0x28 }, { RSI, 0x30 }, { RBX, 0x38 }), 0x40, S);

    T_CASE("the epilogue at every instruction boundary");
    CASE(F1_EPI + 0, S, S, -1, 0, SV({ R12, 0x28 }, { RSI, 0x30 }, { RBX, 0x38 }), 0x40, S);
    CASE(F1_EPI + 4, S, S, -1, 0, SV({ R12, 0 }, { RSI, 8 }, { RBX, 0x10 }), 0x18, S);
    CASE(F1_EPI + 6, S, S, -1, 0, SV({ RSI, 0 }, { RBX, 8 }), 0x10, S);
    CASE(F1_EPI + 7, S, S, -1, 0, SV({ RBX, 0 }), 8, S);
    CASE(F1_EPI + 8, S, S, -1, 0, NONE, 0, S);

    T_CASE("a frame pointer, a large allocation, saves by mov, xmm saves");
    {
        const uint64_t F = S, fp = F + 0x20;            /* F: rsp after the prologue; rbp = F + 0x20 */
#define F2_ALL { XMM + 15, 0x900 }, { RDI, 0x800 }, { XMM + 6, 0x40 }, { RSI, 0x38 }, { RBP, 0x1000 }
        CASE(F2 + 0x80, F - 0x300, F, RBP, fp, SV(F2_ALL), 0x1008, F);        /* the body, after an alloca */
        CASE(F2 + 40, F, F, RBP, fp, SV(F2_ALL), 0x1008, F);
        CASE(F2 + 31, F, F, RBP, fp, SV({ RDI, 0x800 }, { XMM + 6, 0x40 }, { RSI, 0x38 }, { RBP, 0x1000 }), 0x1008, F);
        CASE(F2 + 23, F, F, RBP, fp, SV({ XMM + 6, 0x40 }, { RSI, 0x38 }, { RBP, 0x1000 }), 0x1008, F);
        CASE(F2 + 18, F, F, RBP, fp, SV({ RSI, 0x38 }, { RBP, 0x1000 }), 0x1008, F);
        CASE(F2 + 13, F, F, RBP, fp, SV({ RBP, 0x1000 }), 0x1008, F);
        /* Before `lea rbp`: rbp is still the caller's, and the frame is where rsp is. */
        CASE(F2 + 8, F, F, RBP, 0x1111, SV({ RBP, 0x1000 }), 0x1008, F);
        CASE(F2 + 1, F, F, RBP, 0x1111, SV({ RBP, 0 }), 8, F);
        CASE(F2 + 0, F, F, RBP, 0x1111, NONE, 0, F);
        /* The epilogue restores only what it pops: rsi, rdi and the xmm registers were reloaded before it. */
        CASE(F2_EPI + 0, F - 0x300, F, RBP, fp, SV({ RBP, 0x1000 }), 0x1008, F);
        CASE(F2_EPI + 7, F + 0x1000, F, RBP, fp, SV({ RBP, 0x1000 }), 0x1008, NO_FRAME);
        CASE(F2_EPI + 8, F + 0x1008, F, RBP, 0x1111, NONE, 0x1008, NO_FRAME);
        /* Frame register r13 (a REX.B in the lea), 8-bit displacement. */
        CASE(F3 + 0x20, F - 0x80, F, R13, F + 0x10, SV({ R13, 0x40 }), 0x48, F);
        CASE(F3 + 6, F, F, R13, 0x1111, SV({ R13, 0x40 }), 0x48, F);          /* in the prologue: frame = rsp */
        CASE(F3_EPI, F - 0x80, F, R13, F + 0x10, SV({ R13, 0x40 }), 0x48, F);
        CASE(F3 + 0x38, F - 0x80, F, R13, F + 0x10, NONE, 0x48, F);           /* only the codes would pop r13 */
        /* UWOP_ALLOC_LARGE with the size in 32 bits; `add rsp, imm32`. */
        CASE(F4 + 0x10, S, S, -1, 0, NONE, 0x2000, S);
        CASE(F4 + 0, S, S, -1, 0, NONE, 0, S);
        CASE(F4_EPI, S, S, -1, 0, NONE, 0x2000, S);
        CASE(F4 + 0x30, S, S, -1, 0, SV({ RBX, 0x2000 }), 0x2008, S);         /* only the epilogue pops rbx */
    }

    T_CASE("the endings of an epilogue");
    {
        static const uint32_t yes[] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x90, 0xA0, 0xC0 };
        static const uint32_t no[] = { 0x70, 0x80, 0xB0 };
        for (unsigned i = 0; i < sizeof yes / sizeof *yes; i++)               /* pop rbx, then the return */
            CASE(FT + yes[i], S, S, -1, 0, SV({ RBX, 0 }), 8, S);
        for (unsigned i = 0; i < sizeof no / sizeof *no; i++)                 /* not an epilogue: the codes */
            CASE(FT + no[i], S, S, -1, 0, NONE, 0x28, S);
        CASE(FT + 0xD0, S, S, -1, 0, SV({ R12, 0 }, { RBX, 8 }), 0x10, S);
        CASE(FT + 0xE0, S, S, -1, 0, SV({ RBX, 0x28 }), 0x30, S);
    }

    T_CASE("chained entries");
    CASE(CH + 0x10, S, S, -1, 0, SV({ RSI, 0x10 }, { RBX, 0x20 }), 0x28, S);
    CASE(CH + 0, S, S, -1, 0, SV({ RBX, 0x20 }), 0x28, S);                    /* before the chained save */
    CASE(C2 + 8, S, S, -1, 0, SV({ RDI, 0x18 }, { RSI, 0x10 }, { RBX, 0x20 }), 0x28, S);
    CASE(CH + 0x20, S, S, -1, 0, SV({ RSI, 0x10 }, { RBX, 0x20 }), 0x28, S);  /* a jump inside the function */
    CASE(CH + 0x28, S, S, -1, 0, SV({ RBX, 0 }), 8, S);                       /* a jump to its start: a tail call */
    CASE(PA + 0x10, S, S, -1, 0, SV({ RBX, 0x20 }), 0x28, S);

    T_CASE("machine frames");
    for (int with_code = 0; with_code < 2; with_code++) {
        CONTEXT c;
        KNONVOLATILE_CONTEXT_POINTERS p;
        uint64_t frame = 0, base = 0, pc = B + (with_code ? M1 : M0) + 8;
        const uint64_t at = S + 8;                      /* M0: above its 8-byte allocation; M1: above the error code */
        fill_stack();
        ST64(at, RET_MARK);
        ST64(at + 24, g + 0x7000);
        memset(&c, 0, sizeof c);
        memset(&p, 0, sizeof p);
        c.Rsp = S;
        const RUNTIME_FUNCTION *fn = rt_unwind_lookup(pc, &base);
        T_REQUIRE(fn != NULL);
        T_EQ(rt_unwind_step(UNW_FLAG_NHANDLER, base, pc, fn, &c, NULL, &frame, &p), 0);
        T_TRUE(c.Rip == RET_MARK && c.Rsp == g + 0x7000 && frame == S);
        for (unsigned r = 0; r < 16; r++) T_TRUE(p.IntegerContext[r] == NULL && (r == 4 || (&c.Rax)[r] == 0));
    }

    T_CASE("version 2");
    CASE(V2 + 0x10, S, S, -1, 0, SV({ R12, 0x20 }, { RBX, 0x28 }), 0x30, S);  /* no byte is decoded */
    CASE(V2_END - 8, S, S, -1, 0, SV({ R12, 0x20 }, { RBX, 0x28 }), 0x30, S); /* the add before the epilogue */
    CASE(V2_END - 4, S, S, -1, 0, SV({ R12, 0 }, { RBX, 8 }), 0x10, S);
    CASE(V2_END - 2, S, S, -1, 0, SV({ RBX, 0 }), 8, S);
    CASE(V2_END - 1, S, S, -1, 0, NONE, 0, S);
    CASE(V2_END - 0x20, S, S, -1, 0, SV({ R12, 0 }, { RBX, 8 }), 0x10, S);    /* the second epilogue */
    CASE(V2_END - 0x1E, S, S, -1, 0, SV({ RBX, 0 }), 8, S);
    CASE(V2_END - 0x1C, S, S, -1, 0, SV({ R12, 0x20 }, { RBX, 0x28 }), 0x30, S);   /* just after it */
    CASE(V2 + 3, S, S, -1, 0, SV({ R12, 0 }, { RBX, 8 }), 0x10, S);           /* its prologue */
    CASE(VP + 0x10, S, S, -1, 0, SV({ R12, 0x20 }, { RBX, 0x28 }), 0x30, S);  /* the padding slot is not a push */
    CASE(VP + 3, S, S, -1, 0, SV({ R12, 0 }, { RBX, 8 }), 0x10, S);
    CASE(VP_END - 2, S, S, -1, 0, SV({ RBX, 0 }), 8, S);                      /* in its epilogue */

    T_CASE("the handler is reported, never called");
    {
        CONTEXT c;
        uint64_t base = 0, hdata, pc = B + HF + 0x10;
        const RUNTIME_FUNCTION *fn = rt_unwind_lookup(pc, &base);
        T_REQUIRE(fn != NULL);
#define HSTEP(type, at) (memset(&c, 0, sizeof c), c.Rsp = S, hdata = 0x77, \
                         rt_unwind_step((type), base, (at), fn, &c, &hdata, NULL, NULL))
        T_EQ(HSTEP(UNW_FLAG_EHANDLER, pc), B + HF_HANDLER);
        T_TRUE(hdata == B + U_HF + 12 && memcmp((const void *)(uintptr_t)hdata, "DATA", 4) == 0 && c.Rsp == S + 0x30);
        T_EQ(HSTEP(UNW_FLAG_NHANDLER, pc), 0);
        T_EQ(hdata, 0x77);
        T_EQ(HSTEP(UNW_FLAG_UHANDLER, pc), 0);
        T_EQ(HSTEP(UNW_FLAG_EHANDLER, B + HF), 0);      /* in the prologue */
        T_EQ(hdata, 0x77);
    }

    T_CASE("unwind data that is not understood is fatal for a guest and ends a backtrace");
    {
        static const uint32_t bad[] = { BV, BO, B6, BP, BL, BA, BI };
        T_TRAPS(step_at(BV + 8), "a version other than 1 and 2");
        T_TRAPS(step_at(BO + 8), "an unwind operation code that is not known");
        T_TRAPS(step_at(B6 + 8), "an unwind operation code that is not known");
        T_TRAPS(step_at(BP + 8), "runs past the count of codes");
        T_TRAPS(step_at(BL + 8), "more chained function entries");
        T_TRAPS(step_at(BA + 8), "cannot be unwound");
        for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
            Ctx x = hand_ctx(S);
            uint64_t out[4];
            RtUnwindEnd end = RT_UNWIND_END_MAX;
            T_EQ(rt_unwind_backtrace_body(&x, B + bad[i], out, 4, &end), 1);
            T_TRUE(out[0] == B + bad[i] && end == RT_UNWIND_END_BAD_DATA);
        }
    }

    T_CASE("the return-to-host stub");
    {
        uint64_t base = 0, frame = 0;
        const RUNTIME_FUNCTION *e = rt_unwind_lookup(stub, &base);
        T_REQUIRE(e != NULL);
        T_TRUE(base == ntdll->base && (uint64_t)(uintptr_t)e == ntdll->base + RT_NATIVE_UNWIND);
        T_TRUE(e->BeginAddress == stub - base && e->EndAddress == stub - base + RT_STUB_SIZE);
        T_TRUE(LD32(base + e->UnwindData) == 1);        /* version 1, nothing else */
        T_TRUE(rt_unwind_lookup(stub + RT_STUB_SIZE - 1, NULL) == e && rt_unwind_lookup(stub + RT_STUB_SIZE, NULL) == NULL);

        RtThread *t = RT_THREAD(ctx);
        RtShimFrame *saved = t->shim, above = { NULL, S + 0x200, saved }, below = { NULL, S - 0x100, &above };
        CONTEXT c;
        memset(&c, 0, sizeof c);
        c.Rsp = S;
        c.Rbx = 7;
        T_TRUE(saved == NULL);                          /* a test body is in no shim: the base of the guest calls */
        T_EQ(rt_unwind_step(UNW_FLAG_NHANDLER, base, stub, e, &c, NULL, &frame, NULL), 0);
        T_TRUE(c.Rip == 0 && c.Rsp == S && frame == S && c.Rbx == 7);
        /* Inside a shim whose call lies above the frame: its caller.  A shim frame below is not it. */
        fill_stack();
        ST64(S + 0x200, RET_MARK);
        t->shim = &below;                               /* innermost first */
        c.Rip = stub;
        RUNTIME_FUNCTION copy = *e;                     /* recognised by its value, not its address */
        T_EQ(rt_unwind_step(UNW_FLAG_NHANDLER, base, stub, &copy, &c, NULL, &frame, NULL), 0);
        T_TRUE(c.Rip == RET_MARK && c.Rsp == S + 0x208 && frame == S && c.Rbx == 7);
        t->shim = saved;
    }

    T_CASE("capture");
    {
        Ctx x = hand_ctx(S);
        CONTEXT c;
        ST64(S, RET_MARK);
        x.cf = 1; x.zf = 1; x.of = 1;
        x.mxcsr = 0x1FA0;
        x.fpcw = 0x027F;
        for (unsigned i = 0; i < 16; i++) { x.xmm[i].q[0] = 0x1000 + i; x.xmm[i].q[1] = 0x2000 + i; }
        memset(&c, 0xAA, sizeof c);
        rt_unwind_capture(&x, &c);
        T_TRUE(c.Rip == RET_MARK && c.Rsp == S + 8 && c.Rax == CANARY(0) && c.Rcx == CANARY(1) && c.R15 == CANARY(15));
        T_EQ(c.ContextFlags, 0x10000Fu);
        T_EQ(c.EFlags, 0x1u | 0x2u | 0x40u | 0x200u | 0x800u);
        x.cf = 0; x.zf = 0; x.of = 0; x.pf = 1; x.af = 1; x.sf = 1; x.df = 1;
        CONTEXT d;
        rt_unwind_capture(&x, &d);
        T_EQ(d.EFlags, 0x2u | 0x4u | 0x10u | 0x80u | 0x200u | 0x400u);
        T_TRUE(c.SegCs == 0x33 && c.SegDs == 0x2B && c.SegEs == 0x2B && c.SegFs == 0x53 && c.SegGs == 0x2B && c.SegSs == 0x2B);
        T_EQ(c.MxCsr, 0x1FA0u);
        uint8_t fx[512];
        memset(fx, 0, sizeof fx);
        memcpy(fx, &x.fpcw, 2);
        memcpy(fx + 24, &x.mxcsr, 4);
        memcpy(fx + 28, &(uint32_t){ 0xFFFF }, 4);
        memcpy(fx + 160, x.xmm, 256);
        T_TRUE(memcmp(c.FltSave, fx, 512) == 0);
        /* Not written: the home slots, the debug registers, the vector and debug-control tail. */
        T_TRUE(c.P1Home == UINT64_C(0xAAAAAAAAAAAAAAAA) && c.P6Home == c.P1Home && c.Dr0 == c.P1Home && c.Dr7 == c.P1Home);
        T_TRUE(c.VectorRegister[0] == 0xAA && c.VectorControl == c.P1Home && c.LastExceptionFromRip == c.P1Home);
    }

    T_CASE("a backtrace through three frames that ends cleanly");
    {
        /* F1 called from F2 (after an alloca of 0x100) called from F3 called from host code. */
        const uint64_t A = g + 0x1000, X = A + 8 + 0x48, F = X + 0x100, Y = F + 0x1010;
        const uint64_t r1 = B + F1 + 0x20, r2 = B + F2 + 0x80, r3 = B + F3 + 0x20;
        uint64_t out[8];
        RtUnwindEnd end;
        char text[512], want[512];
#define BUILD(last) do { fill_stack(); ST64(A, r1); ST64(A + 8 + 0x40, r2); ST64(F + 0x1008, r3); ST64(Y + 0x48, (last)); } while (0)
        Ctx x = hand_ctx(A);
        x.r[RBP] = F + 0x20;
        x.r[R13] = Y + 0x10;
        BUILD(stub);
        T_EQ(rt_unwind_backtrace(&x, out, 8), 4);
        T_TRUE(out[0] == r1 && out[1] == r2 && out[2] == r3 && out[3] == stub);
        end = RT_UNWIND_END_MAX;
        T_EQ(rt_unwind_backtrace_at(&x, A, out, 8, &end), 4);
        T_EQ(end, RT_UNWIND_END_BASE);
        size_t n = rt_report_backtrace(text, sizeof text, 0, &x, A, 0);
        snprintf(want, sizeof want, "  guest stack (return addresses, innermost first):\n    #0 t_lift_a.dll+0x%x\n"
                 "    #1 t_lift_a.dll+0x%x\n    #2 t_lift_a.dll+0x%x\n    #3 ntdll.dll!rt_return_to_host\n"
                 "    (the base of this thread's guest calls: the stack is complete)\n", F1 + 0x20, F2 + 0x80, F3 + 0x20);
        T_TRUE(strcmp(text, want) == 0 && n == strlen(want));
        rt_report_backtrace(text, sizeof text, 0, &x, 0, 0);
        T_TRUE(strcmp(text, "  guest stack: not available (no return address on the guest stack)\n") == 0);

        T_EQ(rt_unwind_backtrace_at(&x, A, out, 2, &end), 2);                  /* the caller's limit */
        T_EQ(end, RT_UNWIND_END_MAX);
        /* From inside F1's body, the instruction unknown. */
        Ctx y = x;
        y.r[4] = A + 8;
        T_EQ(rt_unwind_backtrace_body(&y, B + F1, out, 8, &end), 4);
        T_TRUE(out[0] == B + F1 && out[1] == r2 && out[2] == r3 && out[3] == stub && end == RT_UNWIND_END_BASE);
        rt_report_backtrace(text, sizeof text, 0, &y, 0, B + F1);
        T_TRUE(strstr(text, "as if inside the body of t_lift_a.dll+0x1100 (") && strstr(text, "    #0 t_lift_a.dll+0x1100\n"));

        BUILD(g + 0x100);                               /* a return address in no module: not written */
        T_EQ(rt_unwind_backtrace_at(&x, A, out, 8, &end), 3);
        T_EQ(end, RT_UNWIND_END_NO_MODULE);
        BUILD(k32->base + RT_NATIVE_SHIM_STUBS);        /* a native address that is not the stub: written, the end */
        T_EQ(rt_unwind_backtrace_at(&x, A, out, 8, &end), 4);
        T_TRUE(out[3] == k32->base + RT_NATIVE_SHIM_STUBS && end == RT_UNWIND_END_NATIVE);
        BUILD(B + BV + 8);
        T_EQ(rt_unwind_backtrace_at(&x, A, out, 8, &end), 4);
        T_EQ(end, RT_UNWIND_END_BAD_DATA);

        T_CASE("a backtrace never reads what it may not, and never loops");
        BUILD(stub);
        T_EQ(rt_unwind_backtrace_at(&x, ur + 0x100, out, 8, &end), 0);         /* the first read is refused */
        T_EQ(end, RT_UNWIND_END_UNREADABLE);
        T_EQ(rt_unwind_backtrace_at(&x, (uint64_t)(uintptr_t)want, out, 8, &end), 0);   /* host memory */
        T_EQ(end, RT_UNWIND_END_UNREADABLE);
        x.r[RBP] = ur + 0x20;                           /* F2's frame is not readable */
        T_EQ(rt_unwind_backtrace_at(&x, A, out, 8, &end), 2);
        T_TRUE(out[1] == r2 && end == RT_UNWIND_END_UNREADABLE);
        x.r[RBP] = F + 0x20;
        Ctx z = hand_ctx(g + STACK_BYTES - 8);          /* the leaf rule's read ends the region */
        ST64(g + STACK_BYTES - 8, B + F1 + 0x20);
        T_EQ(rt_unwind_backtrace(&z, out, 8), 1);
        /* A machine frame whose stack pointer goes down: the walk stops. */
        fill_stack();
        ST64(A, B + M1 + 8);
        ST64(A + 16, r1);
        ST64(A + 40, A - 0x100);
        T_EQ(rt_unwind_backtrace_at(&x, A, out, 8, &end), 1);
        T_EQ(end, RT_UNWIND_END_RSP);
        /* A frame that returns to itself with the stack pointer where it was would loop: a
         * leaf whose return address is its own, read at a stack pointer that a machine frame
         * keeps resetting.  Bounded by `max` whatever the data says. */
        ST64(A + 16, B + M1 + 8);
        ST64(A + 40, A + 8);
        T_TRUE(rt_unwind_backtrace_at(&x, A, out, 8, &end) <= 8);
    }
}

/* ---- compiler-made unwind data: the lifted corpus --------------------------------------------- */

static const RtShimEntry hook_entry = { .lib = "rttest", .name = "UnwindHook" };
static const int kNonvol[8] = { 3, 5, 6, 7, 12, 13, 14, 15 };
static const int32_t kOne[1] = { 7 };

enum { MODE_WALK, MODE_TRAP, MODE_FAULT_IN_SHIM, MODE_FAULT_IN_LIFTED };
static struct {
    int      mode;
    unsigned depth;
    uint64_t cw, list_sum;              /* corpus exports */
    uint64_t entry_rsp[4];              /* of the hook at each depth (1..3) */
    uint64_t site;                      /* the return address of callback_walk's call */
    uint64_t outer_rsp;
} W;

static uint64_t marker(unsigned level, int reg) { return UINT64_C(0x5000000000) + 0x100 * level + (unsigned)reg; }

static void walk_from_innermost(Ctx *ctx);

/* The callback callback_walk calls.  It behaves as a shim does (a shim frame, the host float
 * mode), so that the stack is what a real callback from a shim leaves: lifted code, the
 * shim, the return-to-host stub, lifted code.  Unlike a real shim it changes the
 * non-volatile registers before it calls back in, as markers for the walk. */
GUEST_FN(hook)
{
    RtShimFrame fr;
    rt_shim_enter(ctx, &hook_entry, &fr);
    unsigned d = ++W.depth;
    W.entry_rsp[d] = fr.entry_rsp;
    if (d < 3 && !(d == 2 && W.mode == MODE_FAULT_IN_LIFTED)) {
        uint64_t keep[8];
        for (unsigned i = 0; i < 8; i++) { keep[i] = ctx->r[kNonvol[i]]; ctx->r[kNonvol[i]] = marker(d, kNonvol[i]); }
        GUEST_CALL(ctx, W.cw, (uint64_t)(uintptr_t)kOne, 1, T_GUEST_ADDR(hook), 0);
        for (unsigned i = 0; i < 8; i++) ctx->r[kNonvol[i]] = keep[i];
    } else if (W.mode == MODE_WALK) {
        walk_from_innermost(ctx);
    } else if (W.mode == MODE_TRAP) {
        rt_unimplemented(ctx, __FILE__, __LINE__, "the unwind report test");
    } else if (W.mode == MODE_FAULT_IN_SHIM) {
        ST64(ur + 0x40, 1);
    } else {
        GUEST_CALL(ctx, W.list_sum, ur + 0x40);         /* list_sum reads its argument: a fault in lifted code */
    }
    W.depth--;
    ctx->r[0] = 1;
    rt_shim_leave(ctx, &fr);
}

static void run_chain(int mode) {
    Ctx *ctx = T_CTX();
    uint64_t keep[8];
    W.mode = mode;
    W.depth = 0;
    W.outer_rsp = ctx->r[4];
    for (unsigned i = 0; i < 8; i++) { keep[i] = ctx->r[kNonvol[i]]; ctx->r[kNonvol[i]] = marker(0, kNonvol[i]); }
    GUEST_CALL(ctx, W.cw, (uint64_t)(uintptr_t)kOne, 1, T_GUEST_ADDR(hook), 0);
    for (unsigned i = 0; i < 8; i++) ctx->r[kNonvol[i]] = keep[i];
}

/* In the innermost hook: capture, then walk as a guest would, with the lookup and the step. */
static void walk_from_innermost(Ctx *ctx) {
    const uint64_t stub = rt_return_to_host_address();
    const RtModule *corpus = rt_module_find("corpus_O1.dll");
    CONTEXT c;
    uint64_t bt[16];
    RtUnwindEnd end = RT_UNWIND_END_MAX;
    unsigned steps = 0;
    rt_unwind_capture(ctx, &c);
    W.site = c.Rip;
    T_TRUE(rt_module_from_address(c.Rip) == corpus && c.Rsp == W.entry_rsp[3] + 8);
    for (int level = 2; level >= 0; level--) {
        /* A frame of callback_walk: compiler-made unwind data. */
        KNONVOLATILE_CONTEXT_POINTERS p;
        uint64_t base = 0, frame = 0, hdata = 0, before = c.Rsp;
        unsigned saved = 0;
        const RUNTIME_FUNCTION *fn = rt_unwind_lookup(c.Rip, &base);
        T_REQUIRE(fn != NULL && base == corpus->base && c.Rip == W.site);
        memset(&p, 0, sizeof p);
        T_EQ(rt_unwind_step(UNW_FLAG_NHANDLER, base, c.Rip, fn, &c, &hdata, &frame, &p), 0);
        steps++;
        T_TRUE(c.Rip == stub && c.Rsp > before && c.Rsp - before < 0x200 && frame >= before && frame < c.Rsp);
        for (unsigned i = 0; i < 8; i++) {              /* what it saved came back as its caller had it */
            int r = kNonvol[i];
            uint64_t at = (uint64_t)(uintptr_t)p.IntegerContext[r];
            if (!at) continue;
            saved++;
            T_TRUE(at >= before && at < c.Rsp && (&c.Rax)[r] == marker((unsigned)level, r));
        }
        T_TRUE(saved >= 3);                             /* v, n, cb, user and the sum live in saved registers */
        /* The stub: the host frames between this guest call and the shim's guest caller are one frame. */
        fn = rt_unwind_lookup(c.Rip, &base);
        T_REQUIRE(fn != NULL);
        before = c.Rsp;
        T_EQ(rt_unwind_step(UNW_FLAG_NHANDLER, base, c.Rip, fn, &c, &hdata, &frame, NULL), 0);
        steps++;
        if (level > 0) T_TRUE(c.Rip == W.site && c.Rsp == W.entry_rsp[level] + 8 && frame == before);
        else T_TRUE(c.Rip == 0 && c.Rsp == before && before <= W.outer_rsp && W.outer_rsp - before <= 0x80);
    }
    T_EQ(steps, 6);                                     /* back at the test's own call */
    T_TRUE(rt_unwind_lookup(c.Rip, NULL) == NULL);

    T_EQ(rt_unwind_backtrace(ctx, bt, 16), 6);
    T_TRUE(bt[0] == W.site && bt[1] == stub && bt[2] == W.site && bt[3] == stub && bt[4] == W.site && bt[5] == stub);
    rt_unwind_backtrace_at(ctx, W.entry_rsp[3], bt, 16, &end);
    T_EQ(end, RT_UNWIND_END_BASE);
}

static void child_chain(void *arg) { run_chain(*(int *)arg); }

TEST_SETUP(core, unwind_corpus) { rt_test_install_copy(RT_TEST_CORPUS_DLL, CORPUS_REL); }
TEST(core, unwind_corpus)
{
    Ctx *ctx = T_CTX();
    RtModule *corpus = rt_module_load_a(ctx, "corpus_O1.dll", 0);
    T_REQUIRE(corpus != NULL);
    W.cw = rt_module_export(corpus, "callback_walk", 0);
    W.list_sum = rt_module_export(corpus, "list_sum", 0);
    ur = rt_vm_alloc(0, 0x10000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE);
    T_REQUIRE(W.cw != 0 && W.list_sum != 0 && ur != 0);

    T_CASE("every row of the corpus table is found and understood");
    {
        /* The table is read here from the mapped header, independently of the unwinder. */
        const uint64_t base = corpus->base, opt = base + LD32(base + IMAGE_DOS_LFANEW) + 0x18;
        const IMAGE_DATA_DIRECTORY *dir = &((const IMAGE_OPTIONAL_HEADER64 *)(uintptr_t)opt)->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        const RUNTIME_FUNCTION *rows = (const RUNTIME_FUNCTION *)(uintptr_t)(base + dir->VirtualAddress);
        unsigned n = dir->Size / 12, found = 0, understood = 0, tried = 0, stepped = 0;
        uint64_t stack = rt_vm_alloc(0, 0x200000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
        T_REQUIRE(n > 50 && stack != 0);
        for (unsigned i = 0; i < n; i++) {
            const uint8_t *info = (const uint8_t *)(uintptr_t)(base + rows[i].UnwindData);
            uint64_t b = 0, out[1], pc = base + rows[i].BeginAddress + info[1];     /* just past the prologue */
            RtUnwindEnd end = RT_UNWIND_END_BAD_DATA;
            found += rt_unwind_lookup(base + rows[i].BeginAddress, &b) == &rows[i]
                  && rt_unwind_lookup(base + rows[i].EndAddress - 1, NULL) == &rows[i] && b == base;
            /* The checked path first: one frame from the function's body over a zeroed stack. */
            Ctx x = hand_ctx(stack + 0x1000);
            rt_unwind_backtrace_body(&x, base + rows[i].BeginAddress, out, 1, &end);
            understood += end != RT_UNWIND_END_BAD_DATA;
            /* Then as a guest would.  A function with a frame register needs a real frame. */
            if ((info[3] & 15) || end != RT_UNWIND_END_MAX || pc >= base + rows[i].EndAddress) continue;
            CONTEXT c;
            memset(&c, 0, sizeof c);
            c.Rsp = stack + 0x1000;
            tried++;
            rt_unwind_step(UNW_FLAG_NHANDLER, base, pc, &rows[i], &c, NULL, NULL, NULL);
            stepped += c.Rsp >= stack + 0x1008 && (c.Rsp & 7) == 0 && c.Rip == 0;
        }
        T_EQ(found, n);
        T_EQ(understood, n);
        T_TRUE(tried > n / 2 && stepped == tried);
    }

    T_CASE("a capture three guest calls deep, walked back to the test's own call");
    run_chain(MODE_WALK);
    T_REQUIRE(W.site != 0 && W.depth == 0);

    T_CASE("the guest stack in the fatal reports");
    {
        static RtTestChild c;
        char want[640], line[64];
        int mode = MODE_TRAP;
        snprintf(line, sizeof line, "corpus_o1.dll+0x%llx", (unsigned long long)(W.site - corpus->base));
        snprintf(want, sizeof want, "  guest stack (return addresses, innermost first):\n    #0 %s\n"
                 "    #1 ntdll.dll!rt_return_to_host\n    #2 %s\n    #3 ntdll.dll!rt_return_to_host\n    #4 %s\n"
                 "    #5 ntdll.dll!rt_return_to_host\n    (the base of this thread's guest calls: the stack is complete)\n",
                 line, line, line);
        rt_test_child_run(child_chain, &mode, &c);
        T_TRUE(T_CHILD_ABORTED(&c) && T_CHILD_SAID(&c, "FATAL unimplemented case in rttest.dll!UnwindHook: the unwind report test"));
        T_TRUE(T_CHILD_SAID(&c, want));
        T_EQ(rt_test_count(c.out, "guest stack"), 1);

        mode = MODE_FAULT_IN_SHIM;                      /* a host signal inside a shim: the same stack, exact */
        rt_test_child_run(child_chain, &mode, &c);
        T_TRUE(T_CHILD_ABORTED(&c) && T_CHILD_SAID(&c, "FATAL host signal") && T_CHILD_SAID(&c, "inside a shim (in_host=1)"));
        T_TRUE(T_CHILD_SAID(&c, want));
        T_EQ(rt_test_count(c.out, "guest stack"), 1);

        mode = MODE_FAULT_IN_LIFTED;                    /* a host signal in lifted code: from the function's body */
        snprintf(want, sizeof want, "  guest stack, unwound from Ctx.rsp as if inside the body of corpus_o1.dll+0x%llx (wrong if it "
                 "was in its prologue or epilogue, or if Ctx is stale):\n    #0 corpus_o1.dll+0x%llx\n"
                 "    #1 ntdll.dll!rt_return_to_host\n    #2 %s\n    #3 ntdll.dll!rt_return_to_host\n    #4 %s\n"
                 "    #5 ntdll.dll!rt_return_to_host\n    (the base of this thread's guest calls: the stack is complete)\n",
                 (unsigned long long)(W.list_sum - corpus->base), (unsigned long long)(W.list_sum - corpus->base), line, line);
        rt_test_child_run(child_chain, &mode, &c);
        T_TRUE(T_CHILD_ABORTED(&c) && T_CHILD_SAID(&c, "FATAL host signal") && T_CHILD_SAID(&c, "in lifted code"));
        T_TRUE(T_CHILD_SAID(&c, want));
        T_EQ(rt_test_count(c.out, "guest stack"), 1);
        if (getenv("RT_SHOW_SAMPLE")) fprintf(stderr, "%s", c.out);
    }
}
