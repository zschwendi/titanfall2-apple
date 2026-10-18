/* The fatal report that takes no lock (rt/crash.h "ONE REPORT"; review finding R14).
 * Interface and the rules for a fatal path: core/report.h.
 *
 * State: one atomic word, the owner of the process's one report (0, or the pthread_t of the
 * thread that claimed it), and the owner's static buffer.  Nothing here takes a lock, calls
 * malloc, or uses a FILE: every function may run in a signal handler.
 */
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "report.h"
#include "rt/com.h"
#include "rt/win/memory.h"

static _Atomic uintptr_t report_owner;
static char report_buf[RT_REPORT_BUF];

RtReportTurn rt_report_begin(void) {
    uintptr_t me = (uintptr_t)pthread_self(), none = 0;
    if (atomic_compare_exchange_strong(&report_owner, &none, me)) {
        /* The watchdog.  SIGALRM goes to any thread that does not block it: make sure this
         * one does not, and that nothing the host program installed swallows it. */
        sigset_t s;
        sigemptyset(&s);
        sigaddset(&s, SIGALRM);
        pthread_sigmask(SIG_UNBLOCK, &s, NULL);
        signal(SIGALRM, SIG_DFL);
        alarm(RT_REPORT_WATCHDOG_S);
        return RT_REPORT_FIRST;
    }
    if (none == me) return RT_REPORT_NESTED;
    for (;;) pause();                   /* the owner is about to end the process */
}

bool rt_report_active(void) { return atomic_load(&report_owner) != 0; }

char *rt_report_buffer(size_t *cap) {
    if (cap) *cap = sizeof report_buf;
    return report_buf;
}

/* The quiet end (rt/crash.h).  Two atomic words: what the program asked for, and whether
 * the signal handler has run. */
static _Atomic int report_quiet;        /* RtReportQuiet */
static _Atomic int report_signal;

void rt_report_quiet(RtReportQuiet q) { atomic_store(&report_quiet, (int)q); }
void rt_report_by_signal(void)        { atomic_store(&report_signal, 1); }

static void quiet_end(int sig) { _exit(128 + sig); }

_Noreturn void rt_report_abort(void) {
    rt_com_counts_print();              /* the calls through interface slots so far (rt/com.h): once, no lock */
    rt_crash_handlers_off();
    /* abort() itself is kept, for what it does before it raises the signal (it flushes the C
     * library's streams, without their locks); the handler only replaces the death. */
    const int q = atomic_load(&report_quiet);
    if (q == RT_QUIET_ALL || (q == RT_QUIET_STOPS && !atomic_load(&report_signal))) signal(SIGABRT, quiet_end);
    abort();
}

_Noreturn void rt_report_die(const char *block) {
    (void)rt_report_begin();            /* FIRST or NESTED: either way this thread prints */
    rt_log_flush_try();
    rt_log_raw(block, strlen(block));
    rt_report_abort();
}

/* ---- text ---------------------------------------------------------------------------------------- */

size_t rt_report_appendf(char *buf, size_t n, size_t at, const char *fmt, ...) {
    if (n == 0) return 0;
    if (at >= n - 1) { buf[n - 1] = 0; return n - 1; }
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + at, n - at, fmt, ap);
    va_end(ap);
    if (w < 0) { buf[at] = 0; return at; }
    return at + (size_t)w < n ? at + (size_t)w : n - 1;
}

size_t rt_report_regs(char *buf, size_t n, size_t at, const uint64_t r[16], const char *indent) {
    static const char *const names[16] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                           "r8 ", "r9 ", "r10", "r11", "r12", "r13", "r14", "r15" };
    for (int i = 0; i < 16; i++)
        at = rt_report_appendf(buf, n, at, "%s%s=%016llx%s", i % 4 == 0 ? indent : " ", names[i],
                               (unsigned long long)r[i], i % 4 == 3 ? "\n" : "");
    return at;
}

static const char *protect_name(DWORD p) {
    switch (p & 0xFFu) {
    case PAGE_NOACCESS:          return "PAGE_NOACCESS";
    case PAGE_READONLY:          return "PAGE_READONLY";
    case PAGE_READWRITE:         return "PAGE_READWRITE";
    case PAGE_WRITECOPY:         return "PAGE_WRITECOPY";
    case PAGE_EXECUTE:           return "PAGE_EXECUTE";
    case PAGE_EXECUTE_READ:      return "PAGE_EXECUTE_READ";
    case PAGE_EXECUTE_READWRITE: return "PAGE_EXECUTE_READWRITE";
    case PAGE_EXECUTE_WRITECOPY: return "PAGE_EXECUTE_WRITECOPY";
    default:                     return "?";
    }
}

size_t rt_report_address(char *buf, size_t n, size_t at, uint64_t addr, const RtThread *t) {
    char where[160];
    RtVmProbe p;
    at = rt_report_appendf(buf, n, at, "  address: ");
    if (addr < 0x10000)
        return rt_report_appendf(buf, n, at, "a null pointer (offset 0x%llx)\n", (unsigned long long)addr);
    if (t && addr >= t->stack_region && addr < t->stack_limit)
        at = rt_report_appendf(buf, n, at, "GUEST STACK OVERFLOW: the guard area below this thread's guest stack "
                               "(0x%llx to 0x%llx); ", (unsigned long long)t->stack_limit,
                               (unsigned long long)t->stack_base);
    RtVmProbeState st = rt_vm_probe(addr, &p);
    if (st == RT_VM_PROBE_OUTSIDE)
        return rt_report_appendf(buf, n, at, "host memory or unmapped, outside the guest arenas\n");
    rt_module_describe(addr, where, sizeof where);
    const char *type = p.type == RT_VM_IMAGE ? "image" : p.type == RT_VM_MAPPED ? "mapped" : "private";
    switch (st) {
    case RT_VM_PROBE_BUSY:
        return rt_report_appendf(buf, n, at, "%s; the VM map is busy (its lock is held), the state of the page "
                                 "is not known\n", where);
    case RT_VM_PROBE_FREE:
        return rt_report_appendf(buf, n, at, "%s, free: in no VM region\n", where);
    case RT_VM_PROBE_RESERVED:
        return rt_report_appendf(buf, n, at, "%s, in the %s region at 0x%llx (0x%llx bytes): reserved, not committed\n",
                                 where, type, (unsigned long long)p.base, (unsigned long long)p.size);
    default:
        return rt_report_appendf(buf, n, at, "%s, in the %s region at 0x%llx (0x%llx bytes): committed, "
                                 "protection 0x%x (%s%s)\n", where, type, (unsigned long long)p.base,
                                 (unsigned long long)p.size, (unsigned)p.protect, protect_name(p.protect),
                                 (p.protect & PAGE_GUARD) ? " | PAGE_GUARD" : "");
    }
}

size_t rt_report_guest(char *buf, size_t n, size_t at, const RtThread *t) {
    char where[160];
    if (!t) return rt_report_appendf(buf, n, at, "  (no guest thread)\n");
    const Ctx *c = &t->ctx;
    at = rt_report_regs(buf, n, at, c->r, "  ");
    at = rt_report_appendf(buf, n, at, "  cf=%u pf=%u af=%u zf=%u sf=%u of=%u df=%u mxcsr=%08x gs_base=%016llx\n",
                           c->cf, c->pf, c->af, c->zf, c->sf, c->of, c->df, c->mxcsr, (unsigned long long)c->gs_base);
    /* The shims the thread is inside, innermost first.  The frames are on this thread's own
     * host stack; at most 16 are followed, in case the chain is damaged. */
    int left = 16;
    for (const RtShimFrame *f = t->shim; f && left--; f = f->prev) {
        if (f->entry_rsp >= t->stack_limit && f->entry_rsp + 8 <= t->stack_base)
            rt_module_describe(LD64(f->entry_rsp), where, sizeof where);
        else
            snprintf(where, sizeof where, "?");
        at = rt_report_appendf(buf, n, at, "  in shim %s.dll!%s, called from %s\n", f->shim->lib, f->shim->name, where);
    }
    return rt_report_entry(buf, n, at, t);
}

size_t rt_report_entry(char *buf, size_t n, size_t at, const RtThread *t) {
    const RtModule *m = NULL;
    DWORD reason = 0;
    if (!t || !rt_loader_in_entry(t, &m, &reason)) return at;
    return rt_report_appendf(buf, n, at, "  inside the entry point of %s (reason %u)\n", m->name, (unsigned)reason);
}

bool rt_report_readable(uint64_t addr, uint64_t len) {
    if (len == 0) len = 1;
    uint64_t last = addr + len - 1;
    if (last < addr) return false;
    for (uint64_t page = addr & ~UINT64_C(0xFFF);; page += 0x1000) {
        RtVmProbe p;
        if (rt_vm_probe(page, &p) != RT_VM_PROBE_COMMITTED) return false;
        if ((p.protect & 0xFFu) == PAGE_NOACCESS || (p.protect & PAGE_GUARD)) return false;
        if (last - page < 0x1000) return true;
    }
}
