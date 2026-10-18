/* Read-only unwinding of the guest stack.  Specification: rt/unwind.h (every case, the
 * decisions, the two ways of reading guest memory); rt_report_backtrace: core/report.h.
 *
 * One interpreter serves both callers.  `Unw` says which: for the Windows-facing functions
 * (checked false) guest memory is read directly and a frame that cannot be explained is
 * fatal in the public wrapper; for a fatal report's backtrace (checked true) every read is
 * probed first with rt_report_readable, no lock is taken (rt_module_from_address, not
 * rt_module_from_pc) and a failure only ends the walk.  A failed read yields 0 and sets
 * Unw.bad; the interpreter goes on with zeros until its next check of `bad`, which is why
 * every loop here is bounded by a count read once (at most 255 codes, 32 chained entries,
 * 32 pops) and never by a value read after a failure.
 *
 * Sources.  The data layouts and the meaning of each unwind code: "x64 exception handling"
 * on learn.microsoft.com.  The order of the steps, the epilogue decoder's byte patterns and
 * version 2 (UWOP_EPILOG) are not in that document.  They were written from memory and have
 * since been compared, rule by rule, with the unwinder Microsoft publishes with the .NET
 * runtime (dotnet/runtime, src/coreclr/unwinder/amd64/unwinder.cpp, a port of
 * RtlVirtualUnwind): the version 1 byte patterns, the tail-call rule for a jump, the
 * version 2 epilogue codes, the handler rule and the chain limit of 32 agree; the slot count
 * of UWOP_EPILOG did not and was corrected (docs/reviews/win-behaviour-check.md).  What
 * RtlCaptureContext stores agrees with Wine's test (docs/reviews/win-behaviour-check-2.md).
 *
 * State: the return-to-host stub's function entry, 16 bytes of guest memory written once
 * (pthread_once) on the first rt_unwind_lookup that asks for it.  Nothing else.
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "core.h"
#include "report.h"
#include "rt/module.h"
#include "rt/thread.h"
#include "rt/unwind.h"

typedef struct Unw {
    bool        checked;        /* a fatal report: probe before every read */
    bool        body;           /* the instruction is not known: every code applies, no epilogue */
    bool        unreadable;     /* `bad` is a read that was refused */
    const char *bad;            /* why this frame cannot be explained, or NULL */
} Unw;

static bool bad(Unw *u, const char *why) {
    if (!u->bad) u->bad = why;
    return false;
}

static bool rd(Unw *u, uint64_t a, void *dst, unsigned n) {
    if (!u->bad && u->checked && !rt_report_readable(a, n)) {
        u->unreadable = true;
        bad(u, "guest memory that cannot be read");
    }
    if (u->bad) { memset(dst, 0, n); return false; }
    memcpy(dst, (const void *)(uintptr_t)a, n);
    return true;
}
static uint8_t  rd8(Unw *u, uint64_t a)  { uint8_t v;  rd(u, a, &v, 1); return v; }
static uint16_t rd16(Unw *u, uint64_t a) { uint16_t v; rd(u, a, &v, 2); return v; }
static uint32_t rd32(Unw *u, uint64_t a) { uint32_t v; rd(u, a, &v, 4); return v; }
static uint64_t rd64(Unw *u, uint64_t a) { uint64_t v; rd(u, a, &v, 8); return v; }

/* ---- the function table of a mapped image ------------------------------------------------------- */

/* The exception directory of the image mapped at `base` (`size` bytes), read from its
 * mapped header: the address of the first row and the number of rows.  False if it has
 * none, or one that does not lie inside the image. */
static bool pdata_of(Unw *u, uint64_t base, uint64_t size, uint64_t *rows, uint32_t *count) {
    uint32_t lfanew = rd32(u, base + IMAGE_DOS_LFANEW);
    if (u->bad || size < 0x18 + sizeof(IMAGE_OPTIONAL_HEADER64) || lfanew > size - 0x18 - sizeof(IMAGE_OPTIONAL_HEADER64))
        return false;
    uint64_t opt = base + lfanew + 0x18;
    if (rd32(u, base + lfanew) != IMAGE_NT_SIGNATURE || rd16(u, opt) != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || rd32(u, opt + offsetof(IMAGE_OPTIONAL_HEADER64, NumberOfRvaAndSizes)) <= IMAGE_DIRECTORY_ENTRY_EXCEPTION)
        return false;
    uint64_t dir = opt + offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) + 8 * IMAGE_DIRECTORY_ENTRY_EXCEPTION;
    uint32_t rva = rd32(u, dir), bytes = rd32(u, dir + 4);
    if (u->bad || rva == 0 || bytes < sizeof(RUNTIME_FUNCTION) || rva > size || bytes > size - rva) return false;
    *rows = base + rva;
    *count = bytes / (uint32_t)sizeof(RUNTIME_FUNCTION);
    return true;
}

/* The guest address of the row that covers `rva`, or 0. */
static uint64_t find_row(Unw *u, uint64_t base, uint64_t size, uint32_t rva) {
    uint64_t rows;
    uint32_t lo = 0, hi;
    if (!pdata_of(u, base, size, &rows, &hi)) return 0;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint64_t row = rows + sizeof(RUNTIME_FUNCTION) * (uint64_t)mid;
        uint32_t begin = rd32(u, row), end = rd32(u, row + 4);
        if (u->bad) return 0;
        if (rva < begin) hi = mid;
        else if (rva >= end) lo = mid + 1;
        else return row;
    }
    return 0;
}

/* ---- unwind information ------------------------------------------------------------------------- */

typedef struct Info {
    uint64_t at;                                        /* guest address of the UNWIND_INFO */
    unsigned version, flags, prolog, count, freg, foff;
} Info;

#define CODE_AT(i, k)  ((i)->at + 4 + 2 * (uint64_t)(k))            /* slot k */
#define TAIL_AT(i)     CODE_AT((i), ((i)->count + 1u) & ~1u)        /* handler RVA, or the parent's entry */

static bool info_read(Unw *u, uint64_t base, const RUNTIME_FUNCTION *f, Info *i) {
    uint8_t h[4];
    memset(i, 0, sizeof *i);
    if (f->UnwindData & 1) return bad(u, "an indirect function entry (bit 0 of UnwindData is set)");
    i->at = base + f->UnwindData;
    if (!rd(u, i->at, h, 4)) return false;
    i->version = h[0] & 7u;
    i->flags = h[0] >> 3;
    i->prolog = h[1];
    i->count = h[2];
    i->freg = h[3] & 15u;
    i->foff = h[3] >> 4;
    if (i->version != 1 && i->version != 2) return bad(u, "unwind information of a version other than 1 and 2");
    return true;
}

/* Slots of one unwind code; 0 for a code that is not understood.  The two version 2 codes
 * are not in the documentation; their counts are those of the unwinder Microsoft publishes
 * with the .NET runtime (dotnet/runtime, src/coreclr/unwinder/amd64/unwinder.cpp), where the
 * walk over the codes steps over UWOP_EPILOG TWO slots at a time and over UWOP_SPARE_CODE
 * three.  So an odd number of epilogue slots is followed by a padding slot, and that slot
 * is skipped whatever it holds (a zero slot would otherwise read as "push rax").  The
 * epilogue scan of epilogue_v2 reads the same slots one at a time, as that unwinder does. */
static unsigned op_slots(const Info *i, unsigned op, unsigned oi) {
    switch (op) {
    case UWOP_PUSH_NONVOL: case UWOP_ALLOC_SMALL: case UWOP_SET_FPREG: case UWOP_PUSH_MACHFRAME: return 1;
    case UWOP_ALLOC_LARGE:     return oi == 0 ? 2 : oi == 1 ? 3 : 0;
    case UWOP_SAVE_NONVOL: case UWOP_SAVE_XMM128: return 2;
    case UWOP_SAVE_NONVOL_FAR: case UWOP_SAVE_XMM128_FAR: return 3;
    case UWOP_EPILOG:          return i->version == 2 ? 2 : 0;
    case UWOP_SPARE_CODE:      return i->version == 2 ? 3 : 0;
    default:                   return 0;
    }
}

/* Slot k of `i`: its prologue offset, operation and info, and the number of slots it takes.
 * False (with `bad` set) for a code that is not understood or runs past the count. */
static bool code_read(Unw *u, const Info *i, unsigned k, unsigned *offset, unsigned *op, unsigned *oi, unsigned *slots) {
    uint8_t c[2];
    if (!rd(u, CODE_AT(i, k), c, 2)) return false;
    *offset = c[0];
    *op = c[1] & 15u;
    *oi = c[1] >> 4;
    *slots = op_slots(i, *op, *oi);
    if (*slots == 0) return bad(u, "an unwind operation code that is not known");
    if (k + *slots > i->count) return bad(u, "an unwind code that runs past the count of codes");
    return true;
}

/* The primary entry of `f`: follow UNW_FLAG_CHAININFO to the entry that is not chained. */
static bool primary_of(Unw *u, uint64_t base, RUNTIME_FUNCTION f, RUNTIME_FUNCTION *pf, Info *pi) {
    for (unsigned chain = 0; chain < RT_UNWIND_CHAIN_MAX; chain++) {
        if (!info_read(u, base, &f, pi)) return false;
        if (!(pi->flags & UNW_FLAG_CHAININFO)) { *pf = f; return true; }
        if (!rd(u, TAIL_AT(pi), &f, sizeof f)) return false;
    }
    return bad(u, "more chained function entries than RT_UNWIND_CHAIN_MAX");
}

static void restore_int(Unw *u, CONTEXT *c, KNONVOLATILE_CONTEXT_POINTERS *p, unsigned reg, uint64_t from) {
    uint64_t v = rd64(u, from);
    if (u->bad) return;
    (&c->Rax)[reg] = v;
    if (p) p->IntegerContext[reg] = (DWORD64 *)(uintptr_t)from;
}

static void restore_xmm(Unw *u, CONTEXT *c, KNONVOLATILE_CONTEXT_POINTERS *p, unsigned reg, uint64_t from) {
    uint8_t v[16];
    if (!rd(u, from, v, 16)) return;
    memcpy(c->FltSave + RT_CONTEXT_FLTSAVE_XMM0 + 16 * reg, v, 16);
    if (p) p->FloatingContext[reg] = (PVOID)(uintptr_t)from;
}

/* Step 1 of rt_unwind_step: the base of the fixed stack allocation.  (Windows looks for the
 * UWOP_SET_FPREG code one slot at a time, so it can mistake the second slot of a longer
 * code for one; this walks code by code.) */
static uint64_t establisher(Unw *u, const Info *i, uint32_t off, const CONTEXT *c) {
    if (i->freg == 0) return c->Rsp;
    uint64_t fp = (&c->Rax)[i->freg] - 16 * (uint64_t)i->foff;
    if (off >= i->prolog || (i->flags & UNW_FLAG_CHAININFO)) return fp;
    for (unsigned k = 0, offset, op, oi, slots; k < i->count; k += slots) {
        if (!code_read(u, i, k, &offset, &op, &oi, &slots)) return 0;
        if (op == UWOP_SET_FPREG) return off >= offset ? fp : c->Rsp;
    }
    return c->Rsp;
}

/* Step 3: undo the codes of `f` and of every entry it is chained to, then pop the return
 * address.  Stores the primary entry for step 4. */
static bool undo_codes(Unw *u, uint64_t base, uint64_t pc, RUNTIME_FUNCTION f, Info i, CONTEXT *c,
                       KNONVOLATILE_CONTEXT_POINTERS *p, uint64_t frame, RUNTIME_FUNCTION *pf, Info *pi) {
    bool machine_frame = false;
    for (unsigned chain = 0;; chain++) {
        uint32_t off = u->body ? UINT32_MAX : (uint32_t)(pc - (base + f.BeginAddress));
        for (unsigned k = 0, offset, op, oi, slots; k < i.count; k += slots) {
            if (!code_read(u, &i, k, &offset, &op, &oi, &slots)) return false;
            uint32_t arg = slots == 2 ? rd16(u, CODE_AT(&i, k + 1)) : slots == 3 ? rd32(u, CODE_AT(&i, k + 1)) : 0;
            if (off < offset) continue;                 /* THE PROLOGUE RULE: this has not happened yet */
            switch (op) {
            case UWOP_PUSH_NONVOL:     restore_int(u, c, p, oi, c->Rsp); c->Rsp += 8; break;
            case UWOP_ALLOC_LARGE:     c->Rsp += oi == 0 ? 8 * (uint64_t)arg : arg; break;
            case UWOP_ALLOC_SMALL:     c->Rsp += 8 * (uint64_t)oi + 8; break;
            case UWOP_SET_FPREG:       c->Rsp = (&c->Rax)[i.freg] - 16 * (uint64_t)i.foff; break;
            case UWOP_SAVE_NONVOL:     restore_int(u, c, p, oi, frame + 8 * (uint64_t)arg); break;
            case UWOP_SAVE_NONVOL_FAR: restore_int(u, c, p, oi, frame + arg); break;
            case UWOP_SAVE_XMM128:     restore_xmm(u, c, p, oi, frame + 16 * (uint64_t)arg); break;
            case UWOP_SAVE_XMM128_FAR: restore_xmm(u, c, p, oi, frame + arg); break;
            case UWOP_PUSH_MACHFRAME: {
                uint64_t at = c->Rsp + (oi ? 8 : 0), rip = rd64(u, at), rsp = rd64(u, at + 24);
                c->Rip = rip;
                c->Rsp = rsp;
                machine_frame = true;
                break;
            }
            default: break;                             /* UWOP_EPILOG, UWOP_SPARE_CODE: nothing to undo */
            }
            if (u->bad) return false;
        }
        if (!(i.flags & UNW_FLAG_CHAININFO)) break;
        if (chain + 1 >= RT_UNWIND_CHAIN_MAX) return bad(u, "more chained function entries than RT_UNWIND_CHAIN_MAX");
        if (!rd(u, TAIL_AT(&i), &f, sizeof f) || !info_read(u, base, &f, &i)) return false;
    }
    if (!machine_frame) {
        c->Rip = rd64(u, c->Rsp);
        c->Rsp += 8;
    }
    *pf = f;
    *pi = i;
    return !u->bad;
}

/* ---- epilogues ---------------------------------------------------------------------------------- */

/* For a jump at the end of what may be an epilogue: does its target (an RVA outside `f`)
 * belong to the same function, in another of its entries and not at its start?  Then the
 * jump stays inside the function and is no tail call. */
static bool same_function(Unw *u, uint64_t base, uint64_t size, const RUNTIME_FUNCTION *f, uint32_t target) {
    uint64_t row = size ? find_row(u, base, size, target) : 0;
    RUNTIME_FUNCTION t, pa, pb;
    Info ia, ib;
    if (!row || !rd(u, row, &t, sizeof t)) return false;
    if (!primary_of(u, base, t, &pa, &ia) || !primary_of(u, base, *f, &pb, &ib)) return false;
    return pa.BeginAddress == pb.BeginAddress && target != pa.BeginAddress;
}

#define EPILOGUE_POPS_MAX 32u

/* Version 1, THE EPILOGUE RULE (rt/unwind.h, step 2).  True if the bytes at `pc` are the
 * rest of an epilogue; they have then been carried out on *c.  The forms are the ones "x64
 * prolog and epilog" (learn.microsoft.com) allows: "add RSP,constant or lea
 * RSP,constant[FPReg], followed by a series of zero or more 8-byte register pops and a
 * return or a jmp".  The byte patterns, the F2 prefix, "rep ret" and the rule for a direct
 * jump are those of Microsoft's published unwinder (dotnet/runtime,
 * src/coreclr/unwinder/amd64/unwinder.cpp), compared one by one. */
static bool epilogue_v1(Unw *u, uint64_t base, uint64_t size, uint64_t pc, const RUNTIME_FUNCTION *f,
                        const Info *i, CONTEXT *c, KNONVOLATILE_CONTEXT_POINTERS *p) {
    enum { NONE, ADD8, ADD32, LEA8, LEA32 } lead = NONE;
    uint64_t at = pc;
    uint8_t b0 = rd8(u, at);
    if ((b0 & 0xFE) == 0x48) {
        uint8_t b1 = rd8(u, at + 1), b2 = rd8(u, at + 2);
        if (b0 == 0x48 && b1 == 0x83 && b2 == 0xC4) lead = ADD8;                /* add rsp, imm8 */
        else if (b0 == 0x48 && b1 == 0x81 && b2 == 0xC4) lead = ADD32;          /* add rsp, imm32 */
        else if (b1 == 0x8D && (b2 & 7) != 4) {                                 /* lea rsp, [fp + disp] */
            unsigned reg = ((b0 & 1u) << 3) | (b2 & 7u);
            if (reg != 0 && reg == i->freg) {
                if ((b2 & 0xF8) == 0x60) lead = LEA8;
                else if ((b2 & 0xF8) == 0xA0) lead = LEA32;
            }
        }
        at += lead == NONE ? 0 : lead == ADD8 || lead == LEA8 ? 4 : 7;
    }
    const uint64_t pops_at = at;
    unsigned npops = 0;
    for (; npops < EPILOGUE_POPS_MAX; npops++) {
        uint8_t x = rd8(u, at);
        if ((x & 0xF8) == 0x58) at += 1;                                        /* pop rax..rdi */
        else if ((x & 0xF0) == 0x40 && (rd8(u, at + 1) & 0xF8) == 0x58) at += 2;   /* REX pop: r8..r15 */
        else break;
    }
    if (rd8(u, at) == 0xF2) at += 1;                                            /* a prefix with no effect */
    uint8_t t0 = rd8(u, at);
    bool in = false;
    if (t0 == 0xC3 || t0 == 0xC2) in = true;                                    /* ret, ret imm16 */
    else if (t0 == 0xF3) in = rd8(u, at + 1) == 0xC3;                           /* rep ret */
    else if (t0 == 0xEB || t0 == 0xE9) {                                        /* jmp rel8, jmp rel32 */
        uint64_t target = t0 == 0xEB ? at + 2 + (uint64_t)(int64_t)(int8_t)rd8(u, at + 1)
                                     : at + 5 + (uint64_t)(int64_t)(int32_t)rd32(u, at + 1);
        uint64_t rva = target - base;
        if (rva >= f->BeginAddress && rva < f->EndAddress)
            in = rva == f->BeginAddress && !(i->flags & UNW_FLAG_CHAININFO);    /* a jump to its own start */
        else
            in = rva > UINT32_MAX || !same_function(u, base, size, f, (uint32_t)rva);
    }
    else if (t0 == 0xFF) in = rd8(u, at + 1) == 0x25;                           /* jmp [rip + disp32] */
    else if ((t0 & 0xF8) == 0x48) in = rd8(u, at + 1) == 0xFF && (rd8(u, at + 2) & 0x38) == 0x20;   /* REX.W jmp r/m */
    if (u->bad || !in) return false;

    switch (lead) {
    case ADD8:  c->Rsp += (uint64_t)(int64_t)(int8_t)rd8(u, pc + 3); break;
    case ADD32: c->Rsp += (uint64_t)(int64_t)(int32_t)rd32(u, pc + 3); break;
    case LEA8:  c->Rsp = (&c->Rax)[i->freg] + (uint64_t)(int64_t)(int8_t)rd8(u, pc + 3); break;
    case LEA32: c->Rsp = (&c->Rax)[i->freg] + (uint64_t)(int64_t)(int32_t)rd32(u, pc + 3); break;
    default: break;
    }
    at = pops_at;
    for (unsigned k = 0; k < npops; k++) {
        uint8_t x = rd8(u, at);
        unsigned reg = x & 7u;
        if ((x & 0xF0) == 0x40) { reg = ((x & 1u) << 3) | (rd8(u, at + 1) & 7u); at += 2; }
        else at += 1;
        restore_int(u, c, p, reg, c->Rsp);
        c->Rsp += 8;
    }
    c->Rip = rd64(u, c->Rsp);                           /* a return, also when the instruction is a jump */
    c->Rsp += 8;
    return !u->bad;
}

/* Version 2.  The format is not in the published documentation ("Version number of the
 * unwind data, currently 1"); this is what Microsoft's published unwinder does (dotnet/runtime,
 * src/coreclr/unwinder/amd64/unwinder.cpp), compared rule by rule.
 * The first code, if it is UWOP_EPILOG, gives the size of every epilogue in its offset
 * byte, and bit 0 of its info says that one epilogue ends the function.  The UWOP_EPILOG
 * slots after it each give, in 12 bits (offset byte, then info as the high 4), how far
 * before the end of the entry another epilogue starts; 0 is padding.  An epilogue is the
 * pops that mirror the UWOP_PUSH_NONVOL codes (1 byte each, 2 for r8-r15) and the return;
 * the stack adjustment before the pops is not part of it.  Windows also knows a pop for a
 * pushed flags register and a machine frame here; both are kernel forms and are refused. */
static bool epilogue_v2(Unw *u, uint64_t base, uint64_t pc, const RUNTIME_FUNCTION *f, const Info *i,
                        CONTEXT *c, KNONVOLATILE_CONTEXT_POINTERS *p) {
    uint8_t code[2];
    if (i->count == 0 || !rd(u, CODE_AT(i, 0), code, 2) || (code[1] & 15) != UWOP_EPILOG) return false;
    const uint32_t esize = code[0], rva = (uint32_t)(pc - base);
    uint32_t start = 0;
    bool in = false;
    if ((code[1] >> 4) & 1) {
        start = f->EndAddress - esize;
        in = rva - start < esize;
    }
    for (unsigned k = 1; !in && k < i->count; k++) {
        if (!rd(u, CODE_AT(i, k), code, 2) || (code[1] & 15) != UWOP_EPILOG) break;
        uint32_t back = code[0] | ((uint32_t)(code[1] >> 4) << 8);
        if (back == 0) break;
        start = f->EndAddress - back;
        in = rva - start < esize;
    }
    if (u->bad || !in) return false;

    RUNTIME_FUNCTION pf;
    Info pi;
    if (!primary_of(u, base, *f, &pf, &pi)) return false;
    const uint32_t done = rva - start;                  /* bytes of the epilogue already executed */
    uint32_t cur = 0;                                   /* where the next pop stands in the epilogue */
    unsigned k = 0, offset, op = 0, oi, slots;
    for (; k < pi.count; k += slots) {                  /* skip to the pushes: the last codes of the array */
        if (!code_read(u, &pi, k, &offset, &op, &oi, &slots)) return false;
        if (op == UWOP_PUSH_NONVOL) break;
        if (op == UWOP_PUSH_MACHFRAME) return bad(u, "a version 2 epilogue with a machine frame");
    }
    for (; k < pi.count; k += slots) {
        if (!code_read(u, &pi, k, &offset, &op, &oi, &slots)) return false;
        if (op != UWOP_PUSH_NONVOL) return bad(u, "a version 2 epilogue with a code after its pushes");
        if (cur >= done) {
            restore_int(u, c, p, oi, c->Rsp);
            c->Rsp += 8;
        }
        cur += oi >= 8 ? 2 : 1;
    }
    c->Rip = rd64(u, c->Rsp);
    c->Rsp += 8;
    return !u->bad;
}

/* ---- one frame ---------------------------------------------------------------------------------- */

static bool step(Unw *u, DWORD handler_type, uint64_t base, uint64_t pc, RUNTIME_FUNCTION f, CONTEXT *c,
                 uint64_t *handler, uint64_t *handler_data, uint64_t *frame_out, KNONVOLATILE_CONTEXT_POINTERS *p) {
    Info i, pi;
    RUNTIME_FUNCTION pf;
    *handler = 0;
    if (!info_read(u, base, &f, &i)) return false;
    uint32_t off = u->body ? UINT32_MAX : (uint32_t)(pc - (base + f.BeginAddress));
    uint64_t frame = establisher(u, &i, off, c);
    if (u->bad) return false;
    if (frame_out) *frame_out = frame;
    if (!u->body) {
        const RtModule *m = rt_module_from_address(base);
        bool in = i.version == 1 ? epilogue_v1(u, base, m && m->base == base ? m->size : 0, pc, &f, &i, c, p)
                                 : epilogue_v2(u, base, pc, &f, &i, c, p);
        if (u->bad) return false;
        if (in) return true;
    }
    if (!undo_codes(u, base, pc, f, i, c, p, frame, &pf, &pi)) return false;
    if (!u->body && (pi.flags & handler_type & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER))
        && (uint32_t)(pc - (base + pf.BeginAddress)) >= pi.prolog) {
        uint32_t rva = rd32(u, TAIL_AT(&pi));
        if (u->bad) return false;
        *handler = base + rva;
        if (handler_data) *handler_data = TAIL_AT(&pi) + 4;
    }
    return true;
}

/* ---- decision 1: the return-to-host stub -------------------------------------------------------- */

static pthread_once_t stub_once = PTHREAD_ONCE_INIT;
static uint64_t stub_entry;             /* guest address of the stub's RUNTIME_FUNCTION */

static bool is_stub(uint64_t pc) {
    uint64_t stub = rt_return_to_host_address();
    return stub != 0 && pc - stub < RT_STUB_SIZE;
}

static void stub_entry_make(void) {
    uint64_t stub = rt_return_to_host_address();
    const RtModule *m = rt_module_from_address(stub);
    if (!m || !m->native) rt_fatal("the return-to-host stub is not in a native module");
    uint64_t e = m->base + RT_NATIVE_UNWIND;
    ST32(e, (uint32_t)(stub - m->base));
    ST32(e + 4, (uint32_t)(stub - m->base) + RT_STUB_SIZE);
    ST32(e + 8, RT_NATIVE_UNWIND + 0x10);
    ST32(e + 0x10, 0x00000001);         /* UNWIND_INFO: version 1, no flags, no prologue, no codes */
    stub_entry = e;
}

/* Unwind the one frame that stands for the host frames above a guest call made by host
 * code.  True if a guest caller was found (the shim frame of `t` above c->Rsp); false with
 * Rip 0 at the base of the thread's guest calls, or with `bad` set. */
static bool stub_step(Unw *u, const RtThread *t, CONTEXT *c) {
    for (const RtShimFrame *f = t ? t->shim : NULL; f; f = f->prev) {
        if (f->entry_rsp <= c->Rsp) continue;
        uint64_t rip = rd64(u, f->entry_rsp);
        if (u->bad) return false;
        c->Rip = rip;
        c->Rsp = f->entry_rsp + 8;
        return true;
    }
    c->Rip = 0;
    return false;
}

/* ---- the Windows-facing functions --------------------------------------------------------------- */

const RUNTIME_FUNCTION *rt_unwind_lookup(uint64_t pc, uint64_t *image_base) {
    Unw u = { 0 };
    char where[96];
    if (image_base) *image_base = 0;
    const RtModule *m = rt_module_from_pc(pc);
    if (!m) return NULL;
    if (image_base) *image_base = m->base;
    if (m->native) {
        if (!is_stub(pc)) return NULL;
        pthread_once(&stub_once, stub_entry_make);
        return (const RUNTIME_FUNCTION *)(uintptr_t)stub_entry;
    }
    uint64_t row = find_row(&u, m->base, m->size, (uint32_t)(pc - m->base));
    if (!row) return NULL;
    if (LD32(row + 8) & 1)
        rt_fatal("RtlLookupFunctionEntry: the function entry of %s is an indirect one (bit 0 of UnwindData is "
                 "set), which is not built (rt/unwind.h, decision 3)", rt_module_describe(pc, where, sizeof where));
    return (const RUNTIME_FUNCTION *)(uintptr_t)row;
}

GUESTFN rt_unwind_step(DWORD handler_type, uint64_t image_base, uint64_t pc, const RUNTIME_FUNCTION *fn,
                       CONTEXT *context, uint64_t *handler_data, uint64_t *establisher_frame,
                       KNONVOLATILE_CONTEXT_POINTERS *pointers) {
    Unw u = { 0 };
    uint64_t handler = 0;
    char where[96];
    if (!fn || is_stub(image_base + fn->BeginAddress)) {
        if (establisher_frame) *establisher_frame = context->Rsp;
        if (fn) {
            stub_step(&u, rt_thread_self(), context);
        } else {
            context->Rip = LD64(context->Rsp);
            context->Rsp += 8;
        }
        return 0;
    }
    if (!step(&u, handler_type, image_base, pc, *fn, context, &handler, handler_data, establisher_frame, pointers))
        rt_fatal("RtlVirtualUnwind: the frame of %s (pc %#llx) cannot be unwound: its unwind data has %s "
                 "(rt/unwind.h, decision 3)", rt_module_describe(image_base + fn->BeginAddress, where, sizeof where),
                 (unsigned long long)pc, u.bad ? u.bad : "?");
    return handler;
}

void rt_unwind_capture(const Ctx *ctx, CONTEXT *context) {
    context->ContextFlags = CONTEXT_FULL | CONTEXT_SEGMENTS;
    context->MxCsr = ctx->mxcsr;
    context->SegCs = 0x33;
    context->SegDs = context->SegEs = context->SegGs = context->SegSs = 0x2B;
    context->SegFs = 0x53;
    /* The lifter's pushfq image (lifter/insns/int_pushf.py; harness/statefix.py agrees). */
    context->EFlags = (uint32_t)ctx->cf | 1u << 1 | (uint32_t)ctx->pf << 2 | (uint32_t)ctx->af << 4
                    | (uint32_t)ctx->zf << 6 | (uint32_t)ctx->sf << 7 | 1u << 9 | (uint32_t)ctx->df << 10
                    | (uint32_t)ctx->of << 11;
    memcpy(&context->Rax, ctx->r, sizeof ctx->r);
    context->Rip = LD64(ctx->r[4]);
    context->Rsp = ctx->r[4] + 8;
    memset(context->FltSave, 0, sizeof context->FltSave);
    memcpy(context->FltSave, &ctx->fpcw, 2);                                    /* control word */
    memcpy(context->FltSave + RT_CONTEXT_FLTSAVE_MXCSR, &ctx->mxcsr, 4);
    memcpy(context->FltSave + RT_CONTEXT_FLTSAVE_MXCSR + 4, &(uint32_t){ 0xFFFF }, 4);   /* MxCsr mask */
    memcpy(context->FltSave + RT_CONTEXT_FLTSAVE_XMM0, ctx->xmm, sizeof ctx->xmm);
}

/* ---- the call stack for a fatal report ---------------------------------------------------------- */

/* `ret_addr_at` not 0: start at the return address there.  Else `body_fn`: inside it. */
static unsigned walk(const Ctx *ctx, uint64_t ret_addr_at, uint64_t body_fn, uint64_t *out, unsigned max,
                     RtUnwindEnd *end_out) {
    Unw u = { .checked = true };
    RtUnwindEnd end = RT_UNWIND_END_MAX;
    const RtThread *t = RT_THREAD(ctx);
    unsigned n = 0;
    CONTEXT c;
    memset(&c, 0, sizeof c);
    memcpy(&c.Rax, ctx->r, sizeof ctx->r);
    if (ret_addr_at) {
        c.Rip = rd64(&u, ret_addr_at);
        c.Rsp = ret_addr_at + 8;
    } else {
        c.Rip = body_fn;
        u.body = true;
    }
    while (!u.bad && n < max) {
        const uint64_t pc = c.Rip, before = c.Rsp;
        const RtModule *m = rt_module_from_address(pc);
        if (!m || !m->loaded) { end = RT_UNWIND_END_NO_MODULE; break; }
        out[n++] = pc;
        if (m->native) {
            if (!is_stub(pc)) { end = RT_UNWIND_END_NATIVE; break; }
            if (!stub_step(&u, t, &c)) { end = RT_UNWIND_END_BASE; break; }
        } else {
            uint64_t handler, row = find_row(&u, m->base, m->size, (uint32_t)(pc - m->base));
            RUNTIME_FUNCTION f;
            if (row && rd(&u, row, &f, sizeof f)) {
                step(&u, UNW_FLAG_NHANDLER, m->base, pc, f, &c, &handler, NULL, NULL, NULL);
            } else {                                    /* a leaf function */
                c.Rip = rd64(&u, c.Rsp);
                c.Rsp += 8;
            }
        }
        u.body = false;
        if (!u.bad && c.Rsp <= before) { end = RT_UNWIND_END_RSP; break; }
    }
    if (u.bad) end = u.unreadable ? RT_UNWIND_END_UNREADABLE : RT_UNWIND_END_BAD_DATA;
    if (end_out) *end_out = end;
    return n;
}

unsigned rt_unwind_backtrace(const Ctx *ctx, uint64_t *out, unsigned max) {
    return walk(ctx, ctx->r[4], 0, out, max, NULL);
}

unsigned rt_unwind_backtrace_at(const Ctx *ctx, uint64_t ret_addr_at, uint64_t *out, unsigned max, RtUnwindEnd *end) {
    return walk(ctx, ret_addr_at, 0, out, max, end);
}

unsigned rt_unwind_backtrace_body(const Ctx *ctx, uint64_t fn, uint64_t *out, unsigned max, RtUnwindEnd *end) {
    return walk(ctx, 0, fn, out, max, end);
}

size_t rt_report_backtrace(char *buf, size_t n, size_t at, const Ctx *ctx, uint64_t ret_addr_at, uint64_t body_fn) {
    static const char *const ends[] = {
        [RT_UNWIND_END_MAX]        = "(more frames are not shown)",
        [RT_UNWIND_END_BASE]       = "(the base of this thread's guest calls: the stack is complete)",
        [RT_UNWIND_END_NATIVE]     = "(ends at an address in a native module)",
        [RT_UNWIND_END_NO_MODULE]  = "(ends: the next return address is in no loaded module)",
        [RT_UNWIND_END_UNREADABLE] = "(ends: guest memory that cannot be read)",
        [RT_UNWIND_END_BAD_DATA]   = "(ends: unwind information that is not understood)",
        [RT_UNWIND_END_RSP]        = "(ends: the stack pointer did not move up)",
    };
    uint64_t frames[RT_REPORT_GUEST_FRAMES];
    RtUnwindEnd end = RT_UNWIND_END_MAX;
    unsigned count;
    char where[160];
    if (!ctx || (!ret_addr_at && !body_fn))
        return rt_report_appendf(buf, n, at, "  guest stack: not available (no return address on the guest stack)\n");
    if (ret_addr_at) {
        count = rt_unwind_backtrace_at(ctx, ret_addr_at, frames, RT_REPORT_GUEST_FRAMES, &end);
        at = rt_report_appendf(buf, n, at, "  guest stack (return addresses, innermost first):\n");
    } else {
        count = rt_unwind_backtrace_body(ctx, body_fn, frames, RT_REPORT_GUEST_FRAMES, &end);
        at = rt_report_appendf(buf, n, at, "  guest stack, unwound from Ctx.rsp as if inside the body of %s (wrong if it "
                               "was in its prologue or epilogue, or if Ctx is stale):\n",
                               rt_module_describe(body_fn, where, sizeof where));
    }
    for (unsigned i = 0; i < count; i++)
        at = rt_report_appendf(buf, n, at, "    #%u %s\n", i, rt_module_describe(frames[i], where, sizeof where));
    return rt_report_appendf(buf, n, at, "    %s\n", ends[end]);
}
