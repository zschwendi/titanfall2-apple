/* Fatal reports (docs/RUNTIME.md sections 2, 9.1, 9.2).
 *
 * Every way the runtime gives up goes through this file, prints one block that starts with
 * "FATAL" and says what was asked for and by whom, and aborts.  Nothing here returns a
 * value in place of the thing that is missing.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "report.h"
#include "rt/machine.h"
#include "rt/vm.h"

/* Every report of this file ends here: written with no lock, one report per process
 * (core/report.h; review finding R14). */
static _Noreturn void die(const char *block) {
    rt_report_die(block);
}

static size_t appendf(char *buf, size_t n, size_t at, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static size_t appendf(char *buf, size_t n, size_t at, const char *fmt, ...) {
    if (at >= n) return at;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + at, n - at, fmt, ap);
    va_end(ap);
    if (w < 0) return at;
    return at + (size_t)w < n ? at + (size_t)w : n - 1;
}

/* A guest stack address we can read without faulting: inside the thread's own stack. */
static bool on_guest_stack(const RtThread *t, uint64_t a) {
    return t && a >= t->stack_limit && a + 8 <= t->stack_base;
}

size_t rt_report_context(char *buf, size_t n, Ctx *ctx, uint64_t ret_addr_at) {
    size_t at = 0;
    char where[160];
    RtThread *t = ctx ? RT_THREAD(ctx) : NULL;
    if (!ctx || !t) return appendf(buf, n, at, "  (no guest thread)\n");
    if (ret_addr_at && on_guest_stack(t, ret_addr_at)) {
        rt_module_describe(LD64(ret_addr_at), where, sizeof where);
        at = appendf(buf, n, at, "  called from %s", where);
    } else {
        at = appendf(buf, n, at, "  called from (unknown: no return address on the guest stack)");
    }
    at = appendf(buf, n, at, "   thread 0x%x \"%s\"\n", t->id, t->name);
    at = appendf(buf, n, at, "  rcx=%016llx rdx=%016llx r8=%016llx r9=%016llx\n",
                 (unsigned long long)ctx->r[1], (unsigned long long)ctx->r[2],
                 (unsigned long long)ctx->r[8], (unsigned long long)ctx->r[9]);
    at = appendf(buf, n, at, "  rax=%016llx rsp=%016llx rbx=%016llx rbp=%016llx\n",
                 (unsigned long long)ctx->r[0], (unsigned long long)ctx->r[4],
                 (unsigned long long)ctx->r[3], (unsigned long long)ctx->r[5]);
    /* The shims the thread is inside, innermost first: the host-side half of the call chain. */
    for (const RtShimFrame *f = t->shim; f; f = f->prev) {
        if (on_guest_stack(t, f->entry_rsp)) rt_module_describe(LD64(f->entry_rsp), where, sizeof where);
        else snprintf(where, sizeof where, "?");
        at = appendf(buf, n, at, "  in shim %s.dll!%s, called from %s\n", f->shim->lib, f->shim->name, where);
    }
    at = rt_report_entry(buf, n, at, t);       /* inside a DllMain or TLS callback: which module's */
    /* The guest callers, unwound from the return address of the call being reported. */
    at = rt_report_backtrace(buf, n, at, ctx, ret_addr_at && on_guest_stack(t, ret_addr_at) ? ret_addr_at : 0, 0);
    return at;
}

_Noreturn void rt_fatal(const char *fmt, ...) {
    char buf[2048];
    size_t at = appendf(buf, sizeof buf, 0, "FATAL runtime: ");
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + at, sizeof buf - at, fmt, ap);
    va_end(ap);
    if (w > 0) at = at + (size_t)w < sizeof buf ? at + (size_t)w : sizeof buf - 1;
    at = appendf(buf, sizeof buf, at, "\n");
    RtThread *t = rt_thread_self();
    if (t) at = appendf(buf, sizeof buf, at, "  thread 0x%x \"%s\"\n", t->id, t->name);
    if (t) rt_report_entry(buf, sizeof buf, at, t);
    die(buf);
}

_Noreturn void rt_core_unimplemented(const char *function, const char *unit) {
    char buf[2048];
    size_t at = appendf(buf, sizeof buf, 0,
                        "FATAL unimplemented runtime core function %s (unit of work: %s)\n", function, unit);
    RtThread *t = rt_thread_self();
    if (t) rt_report_context(buf + at, sizeof buf - at, &t->ctx, t->shim ? t->shim->entry_rsp : 0);
    die(buf);
}

_Noreturn void rt_unimplemented(Ctx *ctx, const char *file, int line, const char *fmt, ...) {
    char buf[4096], what[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);
    /* A Ctx with no RtThread behind it (ctx->host NULL: one a test or an early caller made by
     * hand) is "no guest thread", exactly as a NULL ctx is: report, do not dereference. */
    RtThread *t = ctx ? RT_THREAD(ctx) : NULL;
    const RtShimEntry *e = t && t->shim ? t->shim->shim : NULL;
    size_t at;
    if (e) at = appendf(buf, sizeof buf, 0, "FATAL unimplemented case in %s.dll!%s: %s\n", e->lib, e->name, what);
    else   at = appendf(buf, sizeof buf, 0, "FATAL unimplemented: %s\n", what);
    at = appendf(buf, sizeof buf, at, "  at %s:%d\n", file, line);
    rt_report_context(buf + at, sizeof buf - at, ctx, t && t->shim ? t->shim->entry_rsp : 0);
    die(buf);
}

/* A shim came back with the guest stack pointer moved.  The thunk is about to pop the
 * return address from wherever rsp points, so stop here, where the culprit is known. */
_Noreturn void rt_shim_bad_rsp(Ctx *ctx, const RtShimFrame *f) {
    char buf[2048];
    size_t at = appendf(buf, sizeof buf, 0,
                        "FATAL shim %s.dll!%s changed the guest stack pointer: rsp was %#llx on entry and is "
                        "%#llx on return (off by %lld)\n",
                        f->shim->lib, f->shim->name, (unsigned long long)f->entry_rsp,
                        (unsigned long long)ctx->r[4], (long long)(ctx->r[4] - f->entry_rsp));
    at = appendf(buf, sizeof buf, at, "  a shim must leave ctx->r[4] alone; the generated thunk pops the return address\n");
    rt_report_context(buf + at, sizeof buf - at, ctx, f->entry_rsp);
    die(buf);
}

/* ---- an import we have not implemented (section 9.1) --------------------------------------- */

/* The stack arguments of the call being reported.  An import with no shim has no prototype
 * here, so this prints the eight words above the home space, which are arguments 5 to 12 if
 * the function takes that many (the Windows x64 convention), and after each one that is the
 * address of readable guest memory the 32 bytes there, as eight 32-bit words: enough to read
 * a small array or see where an out-pointer points.  `rsp` points at the return address. */
static size_t stack_args(char *buf, size_t n, size_t at, const RtThread *t, uint64_t rsp) {
    if (!on_guest_stack(t, rsp + 0x28)) return at;
    at = appendf(buf, n, at, "  the stack above the home space (arguments 5 to 12, if the function takes that many):\n");
    for (unsigned i = 0; i < 8 && on_guest_stack(t, rsp + 0x28 + 8 * i); i++) {
        uint64_t v = LD64(rsp + 0x28 + 8 * i);
        at = appendf(buf, n, at, "    arg%-2u %016llx", i + 5, (unsigned long long)v);
        if (v >= 0x10000 && rt_report_readable(v, 32)) {
            at = appendf(buf, n, at, " ->");
            for (unsigned k = 0; k < 8; k++) at = appendf(buf, n, at, " %08x", (unsigned)LD32(v + 4 * k));
        }
        at = appendf(buf, n, at, "\n");
    }
    return at;
}

static _Noreturn void rt_trap_unimplemented(Ctx *ctx, unsigned index) {
    char buf[4096];
    const RtModule *m = NULL;
    const char *name = NULL;
    size_t at;
    if (rt_native_trap_info(index, &m, &name))
        at = appendf(buf, sizeof buf, 0, "FATAL unimplemented import %s!%s\n", m->name, name);
    else
        at = appendf(buf, sizeof buf, 0, "FATAL trap stub %u was called but never created\n", index);
    /* Entered like any function: the caller pushed the return address, rsp points at it. */
    at += rt_report_context(buf + at, sizeof buf - at, ctx, ctx->r[4]);
    stack_args(buf, sizeof buf, at, RT_THREAD(ctx), ctx->r[4]);
    die(buf);
}

/* One host function per trap stub, so that the dispatch table entry alone identifies the
 * import.  4,096 tiny functions made by macro: T3(x) defines the 16 whose index starts
 * with the hex digits x, and so on down. */
#define T0(x)  static void trap_##x(Ctx *ctx) { rt_trap_unimplemented(ctx, 0x##x); }
#define T1(x)  T0(x##0) T0(x##1) T0(x##2) T0(x##3) T0(x##4) T0(x##5) T0(x##6) T0(x##7) \
               T0(x##8) T0(x##9) T0(x##A) T0(x##B) T0(x##C) T0(x##D) T0(x##E) T0(x##F)
#define T2(x)  T1(x##0) T1(x##1) T1(x##2) T1(x##3) T1(x##4) T1(x##5) T1(x##6) T1(x##7) \
               T1(x##8) T1(x##9) T1(x##A) T1(x##B) T1(x##C) T1(x##D) T1(x##E) T1(x##F)
T2(0) T2(1) T2(2) T2(3) T2(4) T2(5) T2(6) T2(7) T2(8) T2(9) T2(A) T2(B) T2(C) T2(D) T2(E) T2(F)
#undef T0
#define T0(x)  trap_##x,
static const LiftedFn trap_pool[RT_TRAP_POOL] = {
    T2(0) T2(1) T2(2) T2(3) T2(4) T2(5) T2(6) T2(7) T2(8) T2(9) T2(A) T2(B) T2(C) T2(D) T2(E) T2(F)
};
#undef T0
#undef T1
#undef T2

LiftedFn rt_trap_pool_fn(unsigned i) {
    if (i >= RT_TRAP_POOL) rt_fatal("more than %d distinct unimplemented imports; raise RT_TRAP_POOL", RT_TRAP_POOL);
    return trap_pool[i];
}

/* ---- a look-up by name in a stubbed game DLL (rt_module_export, core/module.c) ----------------- */

/* A STUBBED MODULE IS A GAME DLL NOBODY HERE READ.  Whether it exports a name is not known,
 * so GetProcAddress on it has two outcomes only: a shim of ours (for originsdk.dll: a posted
 * stand-in handed to rt_init), or this stop.  It never answers NULL: "the DLL does not export
 * this" is a made-up answer about the DLL, and for EA's kit a way around the halting stubs
 * (finding 1 of docs/reviews/night-2026-10-11-code-review.md).  The first line has the form of
 * an unimplemented import (RUNTIME.md 9.1); the context lines name the caller.  The name is the
 * guest's text: bytes that are not printable ASCII are written as '?', so that no line of this
 * report can be made to read as another report. */
_Noreturn void rt_export_unimplemented(const RtModule *m, const char *name, uint32_t ordinal) {
    char buf[4096], what[128];
    if (name) {
        size_t k = 0;
        for (; name[k] && k < sizeof what - 1; k++) what[k] = name[k] >= 0x20 && name[k] < 0x7f ? name[k] : '?';
        what[k] = 0;
    } else {
        snprintf(what, sizeof what, "#%u", ordinal);
    }
    size_t at = appendf(buf, sizeof buf, 0, "FATAL unimplemented import GetProcAddress(%s, %s)\n", m->name, what);
    at = appendf(buf, sizeof buf, at, "  %s is a game DLL that is stubbed, not lifted or read: a look-up by name or ordinal that finds no "
                 "shim of ours stops the run and is never answered NULL (\"not exported\" would be an answer about that DLL)\n", m->name);
    RtThread *t = rt_thread_self();
    if (t) rt_report_context(buf + at, sizeof buf - at, &t->ctx, t->shim ? t->shim->entry_rsp : 0);
    die(buf);
}

/* ---- dispatch miss (section 2) ---------------------------------------------------------------- */

void rt_dispatch_miss(Ctx *ctx) {
    char buf[2048], where[160];
    uint64_t target = RT_THREAD(ctx)->miss_target;
    rt_module_describe(target, where, sizeof where);
    size_t at = appendf(buf, sizeof buf, 0, "FATAL dispatch miss: call to guest address %#llx (%s)\n",
                        (unsigned long long)target, where);
    const RtModule *m = rt_module_from_address(target);
    if (m && m->native)
        at = appendf(buf, sizeof buf, at, "  the address is inside native module %s but is not one of its stubs\n", m->name);
    else if (m)
        at = appendf(buf, sizeof buf, at, "  the address is inside %s but it is not the start of a function the lifter "
                     "found (a function it found and skipped stops with its reason instead)\n", m->name);
    else if (rt_vm_in_vm_arena(target))
        at = appendf(buf, sizeof buf, at, "  the address is in the VM arena: the guest generated code at run time, "
                     "which cannot be executed\n");
    else if (target == 0)
        at = appendf(buf, sizeof buf, at, "  a call through a null function pointer\n");
    else
        at = appendf(buf, sizeof buf, at, "  the address is in no module and not in guest memory\n");
    /* Reached through CALL_INDIRECT (return address pushed) or through a tail call (the
     * caller's own return address is on top): either way rsp points at a code address. */
    rt_report_context(buf + at, sizeof buf - at, ctx, ctx->r[4]);
    die(buf);
}

/* ---- the lifter's TRAP(rva) (harness/runtime.h) -------------------------------------------- */

_Noreturn void rt_trap(Ctx *ctx, uint64_t guest_address) {
    char buf[2048], where[160];
    rt_module_describe(guest_address, where, sizeof where);
    size_t at = appendf(buf, sizeof buf, 0,
                        "FATAL lifted code reached a trap at %s (%#llx): a fault of the guest's own instruction "
                        "(int3, ud2, a divide error, a misaligned operand), or an address the lifter proved unreachable\n",
                        where, (unsigned long long)guest_address);
    rt_report_context(buf + at, sizeof buf - at, ctx, 0);
    die(buf);
}

/* The TRAP of lifted code built without -DTF_EXACT_TRAPS (harness/runtime.h, "A TRAP DOES NOT READ
 * THE REGISTER FILE"): the same stop, with the register file found through the calling thread.
 * Lifted code keeps registers and flags out of memory between calls, so what is printed is the
 * register file as of the last call or return before the trap; the report says so. */
_Noreturn void rt_trap_lazy(uint64_t guest_address) {
    char buf[2048], where[160];
    RtThread *t = rt_thread_self();
    rt_module_describe(guest_address, where, sizeof where);
    size_t at = appendf(buf, sizeof buf, 0,
                        "FATAL lifted code reached a trap at %s (%#llx): a fault of the guest's own instruction "
                        "(int3, ud2, a divide error, a misaligned operand), or an address the lifter proved unreachable\n",
                        where, (unsigned long long)guest_address);
    at = appendf(buf, sizeof buf, at,
                 "  the registers below are those of the last call or return before the trap, not of the trap itself "
                 "(lifted code keeps them out of memory in between; rebuild with RT_LIFT_CFLAGS += -DTF_EXACT_TRAPS "
                 "for the exact ones)\n");
    if (t) rt_report_context(buf + at, sizeof buf - at, &t->ctx, 0);
    else appendf(buf, sizeof buf, at, "  (the thread is not one of the runtime's: no register file to print)\n");
    die(buf);
}

/* ---- a function the lifter skipped (harness/runtime.h; appendix B item 2) --------------------- */

/* The stub the lifter emits for a skipped function ends here, whether a direct call reached
 * it or a call through the dispatch table: the function's row of <module>_table is that stub. */
_Noreturn void rt_trap_skipped(Ctx *ctx, uint64_t guest_address, const char *why, uint64_t why_address) {
    char buf[2048], where[160], why_where[160];
    rt_module_describe(guest_address, where, sizeof where);
    rt_module_describe(why_address, why_where, sizeof why_where);
    size_t at = appendf(buf, sizeof buf, 0,
                        "FATAL call to %s (%#llx), a function the lifter skipped: %s at %s\n",
                        where, (unsigned long long)guest_address, why ? why : "(no reason given)", why_where);
    at = appendf(buf, sizeof buf, at, "  the function has no translation; that is the first reason of its record in the "
                 "lifter's report (<module>.report.json)\n");
    /* Entered like any function: by a call (the return address was pushed) or by a tail jump
     * (the caller's own return address is on top); either way rsp points at a code address. */
    rt_report_context(buf + at, sizeof buf - at, ctx, ctx ? ctx->r[4] : 0);
    die(buf);
}

/* ---- an instruction the lifter does not translate (harness/runtime.h, UNTRANSLATED) ---------- */

/* Lifted code arrived at an instruction that the lifter decoded and has no translation for,
 * inside a function it lifted all the same (lifter/stops.py, docs/DESIGN.md section 3).  The
 * instruction did not run and nothing after it will.
 *
 * `needs` is the lifter's list of the CPUID feature bits a processor must report to execute
 * the instruction: items `NAME=LEAF.REG.BIT` separated by spaces, LEAF in hex, REG one of
 * eax ebx ecx edx; "" when the lifter knows of none.  What the stop means is decided here,
 * from what this runtime's own CPUID answers for those bits (rt_machine_cpuid_bit: the same
 * table rt_cpuid reads), so the report cannot disagree with what the guest was told:
 *   - a needed feature is reported absent: a processor answering as the runtime does raises
 *     an invalid-opcode fault (#UD) here.  The stop stands where that fault would be;
 *   - every needed feature is reported present, or none is known: a real processor executes
 *     the instruction.  The stop is a gap of the lifter. */
_Noreturn void rt_trap_untranslated(Ctx *ctx, uint64_t guest_address, const char *what, const char *needs) {
    static const char *const regs[4] = { "eax", "ebx", "ecx", "edx" };
    char buf[2048], where[160];
    rt_module_describe(guest_address, where, sizeof where);
    size_t at = appendf(buf, sizeof buf, 0,
                        "FATAL lifted code reached an instruction the lifter does not translate: %s at %s (%#llx)\n",
                        what && *what ? what : "(not named)", where, (unsigned long long)guest_address);
    int n = 0, absent = 0, unknown = 0;
    const char *p = needs ? needs : "";
    for (;;) {
        char name[24], reg[4];
        unsigned leaf = 0, bit = 0;
        int used = 0;
        while (*p == ' ') p++;
        if (!*p) break;
        if (sscanf(p, "%23[^= ]=%x.%3[a-z].%u%n", name, &leaf, reg, &bit, &used) != 4) {
            at = appendf(buf, sizeof buf, at, "%sa feature this report cannot read (\"%s\")", n ? ", " : "  it needs ", p);
            n++; unknown++;
            break;
        }
        unsigned r = 0;
        while (r < 4 && strcmp(reg, regs[r]) != 0) r++;
        int v = rt_machine_cpuid_bit(leaf, 0, r, bit);
        at = appendf(buf, sizeof buf, at, "%s%s (CPUID leaf %#x, %s bit %u: %s)", n ? " and " : "  it needs ", name, leaf, reg, bit,
                     v == 1 ? "reported present" : v == 0 ? "reported absent" : "no answer from this runtime");
        n++; absent += v == 0; unknown += v < 0;
        p += used;
    }
    if (absent)
        at = appendf(buf, sizeof buf, at, "\n  a processor that answers CPUID as this runtime does raises an invalid-opcode "
                     "fault (#UD) at this instruction: the stop stands where that fault would be, and the guest took a "
                     "path that a test of the feature should have closed\n");
    else if (unknown)
        at = appendf(buf, sizeof buf, at, "\n  whether a processor answering CPUID as this runtime does would execute it "
                     "cannot be said from that; the instruction has no translation either way\n");
    else if (n)
        at = appendf(buf, sizeof buf, at, "\n  a processor that answers CPUID as this runtime does executes this "
                     "instruction: the stop is a gap of the lifter (the instruction has no translation), not a fault of the guest\n");
    else
        at = appendf(buf, sizeof buf, at, "  the lifter knows of no CPUID feature bit that this instruction waits for, so a "
                     "real processor is taken to execute it: the stop is a gap of the lifter (the instruction has no "
                     "translation), not a fault of the guest\n");
    /* Inside a function: rsp does not point at a return address. */
    rt_report_context(buf + at, sizeof buf - at, ctx, 0);
    die(buf);
}

/* ---- the lifter's RT_CHECK_RSP (harness/runtime.h; section 2, issue 49) --------------------- */

/* A call in lifted code came back to its host caller, but a guest `ret` would not have come
 * back to that call site: rsp is not what it was before the call, or the return address the
 * call pushed is no longer in its slot.  `return_address` names the call site. */
_Noreturn void rt_rsp_mismatch(Ctx *ctx, uint64_t return_address, uint64_t expected_rsp) {
    char buf[2048], where[160], now_where[160];
    rt_module_describe(return_address, where, sizeof where);
    size_t at;
    if (ctx->r[4] != expected_rsp) {
        at = appendf(buf, sizeof buf, 0,
                     "FATAL a call in lifted code did not return to its call site: the call returning to %s (%#llx) "
                     "came back with rsp = %#llx where %#llx was expected (off by %lld)\n",
                     where, (unsigned long long)return_address, (unsigned long long)ctx->r[4],
                     (unsigned long long)expected_rsp, (long long)(ctx->r[4] - expected_rsp));
        at = appendf(buf, sizeof buf, at,
                     "  a longjmp, an exception unwind, a stack switch, a `ret n` the caller did not expect or a "
                     "`ret` to a pushed address passed through this call; none is supported (docs/RUNTIME.md "
                     "section 9.3)\n");
    } else {
        uint64_t now = LD64(expected_rsp - 8);
        rt_module_describe(now, now_where, sizeof now_where);
        at = appendf(buf, sizeof buf, 0,
                     "FATAL a call in lifted code did not return to its call site: the return address of the call "
                     "returning to %s (%#llx) was overwritten with %#llx (%s) before the callee returned\n",
                     where, (unsigned long long)return_address, (unsigned long long)now, now_where);
        at = appendf(buf, sizeof buf, at,
                     "  the guest `ret` went to the new address; lifted code can only return to its host caller\n");
    }
    rt_report_context(buf + at, sizeof buf - at, ctx, 0);
    die(buf);
}
