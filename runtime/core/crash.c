/* Host signals: the report for a fault in lifted code, in a shim or in the runtime
 * (docs/RUNTIME.md section 9.3).  Interface and the meaning of every line: rt/crash.h.
 *
 * State: the handlers, installed once by rt_crash_init; per host thread, the address of
 * its alternate signal stack; and where the host program's image is loaded, read once at
 * start-up so that the handler can print pc as image+offset without asking the dynamic
 * linker.  No lock.
 *
 * The handler runs on the alternate stack with nothing blocked (SA_NODEFER), so that a
 * fault inside the report itself is caught again and ends in one line (RT_REPORT_NESTED)
 * instead of in silence.  It writes the report in two pieces: first everything that takes
 * no lock at all, then the host frames with their symbols, which need dladdr.
 */
#if !defined(__arm64__) && !defined(__aarch64__)
#error "core/crash.c reads the arm64 thread state"
#endif

#include <dlfcn.h>
#include <mach-o/getsect.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include "rt/thread.h"
#include "report.h"

#define ALTSTACK_BYTES  (128u * 1024u)
#define MAX_FRAMES      16
#define NEAR_LIMIT      (UINT64_C(1) << 20)     /* a dispatch entry further below pc than this is not "near" */

static const int crash_signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGFPE, SIGABRT };
#define NSIGNALS (sizeof crash_signals / sizeof crash_signals[0])

static __thread void *tl_altstack;
static uint64_t image_base;             /* where the host program (the image this file is in) is loaded */
static uint64_t image_text;             /* the size of its __TEXT segment: pc inside it is printed as image+offset */
static char     image_name[64];

static const char *signal_name(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGILL:  return "SIGILL";
    case SIGTRAP: return "SIGTRAP";
    case SIGFPE:  return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    default:      return "signal";
    }
}

static const char *base_name(const char *path) {
    const char *slash = path ? strrchr(path, '/') : NULL;
    return slash ? slash + 1 : path ? path : "?";
}

/* The dispatch table read backwards.  It is written only during rt_init, on one thread,
 * and never freed, so reading it here needs no lock. */

/* The guest address whose host function is exactly `fn`, or 0. */
static uint64_t guest_of_fn(uint64_t fn) {
    if (!g_disp || !fn) return 0;
    for (uint64_t i = 0; i <= g_disp_mask; i++)
        if (g_disp[i].guest && (uint64_t)(uintptr_t)g_disp[i].fn == fn) return g_disp[i].guest;
    return 0;
}

/* The entry whose host function starts nearest at or below `pc`: its guest address, and the
 * function's host address in *fn.  0 if there is none within NEAR_LIMIT. */
static uint64_t guest_near_pc(uint64_t pc, uint64_t *fn) {
    uint64_t best = 0, guest = 0;
    if (!g_disp) return 0;
    for (uint64_t i = 0; i <= g_disp_mask; i++) {
        uint64_t f = (uint64_t)(uintptr_t)g_disp[i].fn;
        if (g_disp[i].guest && f <= pc && f > best) { best = f; guest = g_disp[i].guest; }
    }
    if (!guest || pc - best > NEAR_LIMIT) return 0;
    *fn = best;
    return guest;
}

/* The calling thread's host stack, as [lo, hi). */
static void host_stack(uint64_t *lo, uint64_t *hi) {
    pthread_t self = pthread_self();
    *hi = (uint64_t)(uintptr_t)pthread_get_stackaddr_np(self);
    *lo = *hi - pthread_get_stacksize_np(self);
}

/* The return addresses of the interrupted host frames: pc first, then lr if it is not the
 * first saved return address (the caller of a leaf function), then the frame-pointer
 * chain, followed only while it stays inside the thread's host stack and moves upwards. */
static unsigned host_frames(uint64_t pc, uint64_t lr, uint64_t fp, uint64_t out[MAX_FRAMES], int *lr_at) {
    uint64_t lo, hi;
    unsigned n = 0;
    host_stack(&lo, &hi);
    out[n++] = pc;
    *lr_at = -1;
    bool ok = fp >= lo && fp + 16 <= hi && (fp & 7) == 0;
    if (lr && (!ok || LD64(fp + 8) != lr)) { *lr_at = (int)n; out[n++] = lr; }
    while (ok && n < MAX_FRAMES) {
        uint64_t next = LD64(fp), ret = LD64(fp + 8);
        if (!ret) break;
        out[n++] = ret;
        ok = next > fp && next + 16 <= hi && (next & 7) == 0;
        fp = next;
    }
    return n;
}

static void on_signal(int sig, siginfo_t *si, void *uctx) {
    const ucontext_t *uc = uctx;
    uint64_t pc = 0, lr = 0, sp = 0, fp = 0;
    if (uc && uc->uc_mcontext) {
        pc = (uint64_t)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
        lr = (uint64_t)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
        sp = (uint64_t)__darwin_arm_thread_state64_get_sp(uc->uc_mcontext->__ss);
        fp = (uint64_t)__darwin_arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
    }
    uint64_t addr = si ? (uint64_t)(uintptr_t)si->si_addr : 0;
    int code = si ? si->si_code : 0;

    rt_report_by_signal();              /* whoever owns the report: this process does not end as a mere stop */
    if (rt_report_begin() == RT_REPORT_NESTED) {
        char line[240];
        rt_crash_handlers_off();        /* a third failure ends the process at once */
        size_t k = rt_report_appendf(line, sizeof line, 0,
                                     "FATAL second failure on the reporting thread: %s (%d) at address 0x%016llx, "
                                     "host pc 0x%llx; the report above is incomplete\n",
                                     signal_name(sig), sig, (unsigned long long)addr, (unsigned long long)pc);
        rt_log_raw(line, k);
        rt_report_abort();
    }

    size_t cap, at = 0;
    char *buf = rt_report_buffer(&cap);
    char where[160];
    const RtThread *t = rt_thread_self();

    if (sig == SIGABRT)
        at = rt_report_appendf(buf, cap, at, "FATAL host abort(): SIGABRT (6) raised outside a fatal report (the C "
                               "library or a host library gave up)\n");
    else
        at = rt_report_appendf(buf, cap, at, "FATAL host signal %s (%d), code %d, fault address 0x%016llx\n",
                               signal_name(sig), sig, code, (unsigned long long)addr);
    if (sig == SIGSEGV || sig == SIGBUS) {
        uint64_t lo, hi;
        host_stack(&lo, &hi);
        at = rt_report_address(buf, cap, at, addr, t);
        if (addr < lo && lo - addr <= (UINT64_C(1) << 20))
            at = rt_report_appendf(buf, cap, at, "  HOST STACK OVERFLOW: the address is just below this thread's host "
                                   "stack (0x%llx to 0x%llx)\n", (unsigned long long)lo, (unsigned long long)hi);
    }
    if (t)
        at = rt_report_appendf(buf, cap, at, "  thread 0x%x \"%s\", %s\n", t->id, t->name,
                               t->in_host ? "inside a shim (in_host=1)"
                                          : "in lifted code, or in host code outside a shim (in_host=0)");
    else
        at = rt_report_appendf(buf, cap, at, "  not a guest thread (host thread %p)\n", (void *)pthread_self());
    at = rt_report_appendf(buf, cap, at, "  host pc=0x%llx lr=0x%llx sp=0x%llx fp=0x%llx", (unsigned long long)pc,
                           (unsigned long long)lr, (unsigned long long)sp, (unsigned long long)fp);
    if (image_text && pc >= image_base && pc - image_base < image_text)
        at = rt_report_appendf(buf, cap, at, " (pc is %s+0x%llx)", image_name, (unsigned long long)(pc - image_base));
    at = rt_report_appendf(buf, cap, at, "\n");

    uint64_t near_fn = 0, near = guest_near_pc(pc, &near_fn);
    if (near)
        at = rt_report_appendf(buf, cap, at, "  nearest dispatch entry below pc: %s +0x%llx (a hint: frame #0 below "
                               "is exact)\n", rt_module_describe(near, where, sizeof where),
                               (unsigned long long)(pc - near_fn));
    else
        at = rt_report_appendf(buf, cap, at, "  nearest dispatch entry below pc: none within 1 MiB (pc is not in "
                               "lifted code or a shim thunk)\n");
    if (t) {
        at = rt_report_guest(buf, cap, at, t);
        at = rt_report_appendf(buf, cap, at, "  (guest registers as last stored in Ctx; lifted code may hold newer "
                               "values in host registers)\n");
    }
    /* Inside a shim the guest stack is exact: it starts at the return address of the shim's
     * call.  In lifted code it needs the function, which only the frame list below knows. */
    const bool in_shim = t && t->in_host && t->shim;
    if (in_shim) at = rt_report_backtrace(buf, cap, at, &t->ctx, t->shim->entry_rsp, 0);
    rt_log_flush_try();
    rt_log_raw(buf, at);

    /* The second piece: dladdr may take the dynamic linker's lock.  Everything above is
     * already written, and the watchdog of rt_report_begin is running. */
    uint64_t frames[MAX_FRAMES], inner_guest = 0;       /* inner_guest: the innermost frame that is guest code */
    int lr_at;
    unsigned nframes = host_frames(pc, lr, fp, frames, &lr_at);
    at = rt_report_appendf(buf, cap, 0, "  host frames (a lifted call is a host call, so these are the guest callers too):\n");
    for (unsigned i = 0; i < nframes; i++) {
        Dl_info info;
        uint64_t a = frames[i];
        const char *tag = (int)i == lr_at ? " (lr)" : "";
        if (!dladdr((void *)(uintptr_t)(i ? a - 4 : a), &info) || !info.dli_sname) {
            at = rt_report_appendf(buf, cap, at, "    #%u%s 0x%llx ?\n", i, tag, (unsigned long long)a);
            continue;
        }
        uint64_t start = (uint64_t)(uintptr_t)info.dli_saddr, guest = guest_of_fn(start);
        if (guest && !inner_guest) inner_guest = guest;
        if (guest)
            at = rt_report_appendf(buf, cap, at, "    #%u%s 0x%llx %s  [%s+0x%llx]\n", i, tag, (unsigned long long)a,
                                   rt_module_describe(guest, where, sizeof where), info.dli_sname,
                                   (unsigned long long)(a - start));
        else
            at = rt_report_appendf(buf, cap, at, "    #%u%s 0x%llx %s+0x%llx (%s)\n", i, tag, (unsigned long long)a,
                                   info.dli_sname, (unsigned long long)(a - start), base_name(info.dli_fname));
    }
    if (t && !in_shim) {
        const RtModule *gm = inner_guest ? rt_module_from_address(inner_guest) : NULL;
        if (gm && !gm->native)
            at = rt_report_backtrace(buf, cap, at, &t->ctx, 0, inner_guest);
        else
            at = rt_report_appendf(buf, cap, at, "  guest stack: not unwound (the innermost guest frame above is not "
                                   "a lifted function)\n");
    }
    rt_log_raw(buf, at);
    rt_report_abort();
}

void rt_crash_handlers_off(void) {
    for (unsigned i = 0; i < NSIGNALS; i++) signal(crash_signals[i], SIG_DFL);
}

void rt_crash_thread_attach(void) {
    if (tl_altstack) return;
    void *p = mmap(NULL, ALTSTACK_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) rt_fatal("cannot map an alternate signal stack (%u bytes)", ALTSTACK_BYTES);
    stack_t ss = { .ss_sp = p, .ss_size = ALTSTACK_BYTES, .ss_flags = 0 };
    if (sigaltstack(&ss, NULL) != 0) rt_fatal("sigaltstack failed");
    tl_altstack = p;
}

/* macOS does not carry a thread's alternate stack setting across fork (measured: in the
 * child, sigaltstack reports SS_DISABLE), though the stack's memory is copied.  The test
 * harness forks for every death test, so the one thread of a child sets its stack again. */
static void altstack_after_fork(void) {
    if (!tl_altstack) return;
    stack_t ss = { .ss_sp = tl_altstack, .ss_size = ALTSTACK_BYTES, .ss_flags = 0 };
    sigaltstack(&ss, NULL);
}

void rt_crash_thread_detach(void) {
    if (!tl_altstack) return;
    stack_t ss = { .ss_sp = tl_altstack, .ss_size = ALTSTACK_BYTES, .ss_flags = SS_DISABLE };
    sigaltstack(&ss, NULL);
    munmap(tl_altstack, ALTSTACK_BYTES);
    tl_altstack = NULL;
}

/* The thread unit's two hooks (rt/thread.h, "hooks other units fill"): these replace the weak
 * no-ops of core/thread.c, so every guest thread gets its alternate stack before guest code
 * and gives it back as the last thing its exit path does.  Both calls are idempotent. */
void rt_crash_thread_enter(RtThread *t) { (void)t; rt_crash_thread_attach(); }
void rt_crash_thread_leave(RtThread *t) { (void)t; rt_crash_thread_detach(); }

void rt_crash_init(void) {
    const char *uef = getenv("RT_CALL_UEF");
    if (uef && *uef && strcmp(uef, "0") != 0)
        rt_fatal("RT_CALL_UEF=%s: calling the guest's unhandled-exception filter from a fatal report is not built "
                 "(RUNTIME.md 9.3); unset it", uef);

    Dl_info info;
    if (dladdr((void *)(uintptr_t)&rt_crash_init, &info) && info.dli_fbase) {
        unsigned long size = 0;
        image_base = (uint64_t)(uintptr_t)info.dli_fbase;
        if (getsegmentdata((const struct mach_header_64 *)info.dli_fbase, "__TEXT", &size)) image_text = size;
        snprintf(image_name, sizeof image_name, "%s", base_name(info.dli_fname));
    }

    rt_crash_thread_attach();
    if (pthread_atfork(NULL, NULL, altstack_after_fork) != 0) rt_fatal("pthread_atfork failed");
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    for (unsigned i = 0; i < NSIGNALS; i++)
        if (sigaction(crash_signals[i], &sa, NULL) != 0) rt_fatal("cannot install the handler for %s", signal_name(crash_signals[i]));
}
