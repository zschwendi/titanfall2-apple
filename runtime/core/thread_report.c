/* THE WAIT REPORT of every guest thread (rt/thread.h, rt_thread_report_waits): what a run
 * that does not end says about itself when the driver's time limit stops it.  Part of the
 * thread unit (it walks the list of running threads and reads RtThreadPriv); the lines about
 * a thread's wait are the wait core's (rt_wait_report_thread, rt/sync.h), the caller and the
 * guest stack are the fatal report's (rt_report_context, core/report.h).
 *
 * IT RUNS IN A SIGNAL HANDLER, on whichever host thread took the signal, while every other
 * thread goes on.  So:
 *   - nothing is allocated, nothing is logged, no floating-point number is formatted; each
 *     thread's block is built in one static buffer and written with write(2);
 *   - the two locks it wants, the thread lock (the list, and with it every listed thread's
 *     guest memory) and the dispatcher lock (the wait records), are only TRIED, for up to
 *     200 ms each: the signal may have interrupted a thread inside either.  Without a lock
 *     the report goes on and says so;
 *   - a thread other than the reporter is SUSPENDED (thread_suspend on its Mach port) while
 *     its block is written and resumed afterwards, so that its shim frames, which live on
 *     its host stack, and its guest stack hold still.  Both locks are taken before the
 *     suspension, so a suspended thread never holds one of them;
 *   - a caller that must end whatever happens arms its own second limit first (the driver:
 *     alarm() again with the default action), in case a suspended thread holds something
 *     this code needs after all (the unwinder's one-time table, for one).
 *
 * A THREAD THAT IS NOT INSIDE A WINDOWS FUNCTION is running the game's code: its Ctx is in
 * use, the guest rsp in it is as of the last call the lifted code made, and no return address
 * is known to sit at it.  For such a thread the stack is SCANNED: from Ctx.rsp upwards, the
 * first 8 bytes that are an address inside a function of a loaded module and from which the
 * unwinder reaches the base of the thread's guest calls are taken as the innermost return
 * address.  The frames above it are then exact; the innermost one may be a call that has
 * already returned.  The report says which way each stack was found.
 */
#include <mach/mach.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "thread_priv.h"
#include "report.h"
#include "rt/module.h"
#include "rt/shim.h"
#include "rt/unwind.h"

#define TR_FRAMES   48          /* guest frames printed for a thread */
#define TR_SCAN     4096        /* stack slots looked at for the innermost return address */

static void tr_write(int fd, const char *buf, size_t len) {
    while (len) {
        ssize_t w = write(fd, buf, len);
        if (w <= 0) return;
        buf += w;
        len -= (size_t)w;
    }
}

/* "host thread waiting, 1234 ms of CPU so far" from the kernel, or nothing. */
static size_t tr_host(char *buf, size_t n, size_t at, uint32_t port) {
    static const char *const states[] = { "?", "running", "stopped", "waiting", "waiting (uninterruptible)", "halted" };
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    if (!port || thread_info(port, THREAD_BASIC_INFO, (thread_info_t)&info, &count) != KERN_SUCCESS) return at;
    unsigned long long ms = (unsigned long long)info.user_time.seconds * 1000u + (unsigned)info.user_time.microseconds / 1000u
                          + (unsigned long long)info.system_time.seconds * 1000u + (unsigned)info.system_time.microseconds / 1000u;
    return rt_report_appendf(buf, n, at, "; host thread %s, %llu ms of CPU so far",
                             info.run_state >= 1 && info.run_state <= 5 ? states[info.run_state] : states[0], ms);
}

/* The guest stack of a thread that is inside no shim (the comment at the top). */
static size_t tr_scan(char *buf, size_t n, size_t at, const RtThread *t) {
    uint64_t frames[TR_FRAMES];
    char where[160];
    const uint64_t rsp = t->ctx.r[4] & ~UINT64_C(7);
    uint64_t best_at = 0;
    unsigned best = 0;
    RtUnwindEnd best_end = RT_UNWIND_END_MAX;
    if (rsp < t->stack_limit || rsp >= t->stack_base)
        return rt_report_appendf(buf, n, at, "  guest stack: not available (Ctx.rsp %#llx is outside the thread's guest stack)\n",
                                 (unsigned long long)t->ctx.r[4]);
    for (uint64_t a = rsp, looked = 0; a + 8 <= t->stack_base && looked < TR_SCAN; a += 8, looked++) {
        if (!rt_report_readable(a, 8)) break;
        uint64_t base;
        if (!rt_unwind_lookup(LD64(a), &base)) continue;
        RtUnwindEnd end = RT_UNWIND_END_MAX;
        unsigned count = rt_unwind_backtrace_at(&t->ctx, a, frames, TR_FRAMES, &end);
        if (end == RT_UNWIND_END_BASE || (end == RT_UNWIND_END_MAX && count == TR_FRAMES)) {
            best_at = a;
            best = count;
            best_end = end;
            break;
        }
    }
    if (!best_at)
        return rt_report_appendf(buf, n, at, "  guest stack: not found (no address in the %u slots above Ctx.rsp %#llx unwinds to the thread's base)\n",
                                 TR_SCAN, (unsigned long long)rsp);
    at = rt_report_appendf(buf, n, at, "  guest stack, SCANNED (the thread is in the game's code; the return address was found %llu bytes above "
                           "Ctx.rsp; frame #0 may be a call that has returned):\n", (unsigned long long)(best_at - rsp));
    for (unsigned i = 0; i < best; i++)
        at = rt_report_appendf(buf, n, at, "    #%u %s\n", i, rt_module_describe(frames[i], where, sizeof where));
    return rt_report_appendf(buf, n, at, "    %s\n", best_end == RT_UNWIND_END_BASE ? "(the base of this thread's guest calls: the stack is complete)"
                                                                                      : "(more frames are not shown)");
}

/* `host`: the text of tr_host, taken before the thread was suspended ("" for none). */
static size_t tr_one(char *buf, size_t n, size_t at, RtThread *t, const char *host) {
    const RtThreadPriv *p = RT_THREAD_PRIV(t);
    at = rt_report_appendf(buf, n, at, "guest thread 0x%x \"%s\"%s%s\n", t->id, t->name, t->is_main ? " (guest thread 1)" : "", host);
    if (p && p->suspend_count)
        at = rt_report_appendf(buf, n, at, "  SUSPENDED: created suspended and not resumed yet (it has run no guest code)\n");
    at = rt_wait_report_thread(buf, n, at, t);
    if (t->shim) {
        /* Inside a Windows function: the caller, the argument registers, the shims, the guest
         * stack from the return address.  Then the first argument's memory: for a wait on a
         * lock the game embeds in its own data (EnterCriticalSection, SleepConditionVariable*,
         * AcquireSRWLock*: core/lock.c keeps no record per thread) rcx is the lock, and a
         * CRITICAL_SECTION's third 8 bytes are its owner's thread id. */
        const uint64_t ret_at = t->shim->entry_rsp;
        at += rt_report_context(buf + at, n - at, &t->ctx, ret_at);
        if (ret_at >= t->stack_limit && ret_at + 8 <= t->stack_base) {         /* the report prints 16 frames: the rest, up to TR_FRAMES */
            uint64_t frames[TR_FRAMES];
            char where[160];
            RtUnwindEnd end = RT_UNWIND_END_MAX;
            unsigned count = rt_unwind_backtrace_at(&t->ctx, ret_at, frames, TR_FRAMES, &end);
            if (count > RT_REPORT_GUEST_FRAMES) {
                at = rt_report_appendf(buf, n, at, "  the guest stack goes on:\n");
                for (unsigned i = RT_REPORT_GUEST_FRAMES; i < count; i++)
                    at = rt_report_appendf(buf, n, at, "    #%u %s\n", i, rt_module_describe(frames[i], where, sizeof where));
                at = rt_report_appendf(buf, n, at, "    %s\n", end == RT_UNWIND_END_BASE ? "(the base of this thread's guest calls: the stack is complete)"
                                       : count == TR_FRAMES ? "(more frames are not shown)" : "(the walk ended here)");
            }
        }
        const uint64_t rcx = t->ctx.r[1];
        if (rt_report_readable(rcx, 40))
            at = rt_report_appendf(buf, n, at, "  memory at rcx: %016llx %016llx %016llx %016llx %016llx\n",
                                   (unsigned long long)LD64(rcx), (unsigned long long)LD64(rcx + 8), (unsigned long long)LD64(rcx + 16),
                                   (unsigned long long)LD64(rcx + 24), (unsigned long long)LD64(rcx + 32));
    } else if (t->teb && t->stack_base) {
        at = rt_report_appendf(buf, n, at, "  not inside a Windows function: running the game's code (or not started, or ending)\n");
        at = tr_scan(buf, n, at, t);
    }
    return at;
}

size_t rt_thread_report_one(char *buf, size_t n, size_t at, RtThread *t) {
    char host[96] = "";
    const RtThreadPriv *p = t ? RT_THREAD_PRIV(t) : NULL;
    if (!t) return rt_report_appendf(buf, n, at, "guest thread: none\n");
    if (p) tr_host(host, sizeof host, 0, p->host_port);
    return tr_one(buf, n, at, t, host);
}

void rt_thread_report_waits(int fd) {
    static char buf[16384];
    const uint32_t self_port = pthread_mach_thread_np(pthread_self());
    bool list_locked = false;
    for (unsigned i = 0; i < 200 && !(list_locked = rt_thread_list_trylock()); i++) usleep(1000);
    const bool disp_locked = rt_wait_report_lock();

    unsigned count = 0;
    for (const RtThread *t = rt_thread_list_head_unlocked(); t && count < 100000; t = t->next) count++;
    size_t at = rt_report_appendf(buf, sizeof buf, 0, "THE WAITS OF EVERY GUEST THREAD (%u running; newest first)%s%s\n", count,
                                  list_locked ? "" : "; the thread lock could not be taken in 200 ms: the list was read without it",
                                  disp_locked ? "" : "; the dispatcher lock was not taken (see each thread)");
    at = rt_wait_report_lateness(buf, sizeof buf, at);
    tr_write(fd, buf, at);
    for (RtThread *t = rt_thread_list_head_unlocked(); t; t = t->next) {
        const RtThreadPriv *p = RT_THREAD_PRIV(t);
        const uint32_t port = p ? p->host_port : 0;
        char host[96] = "";
        tr_host(host, sizeof host, 0, port);                    /* its state before it is held still */
        const bool suspended = port && port != self_port && thread_suspend(port) == KERN_SUCCESS;
        at = tr_one(buf, sizeof buf, 0, t, host);
        if (!suspended)
            at = rt_report_appendf(buf, sizeof buf, at, "%s", port == self_port && port
                                   ? "  (the report was written on this thread: it is where the signal found it)\n"
                                   : "  (this thread could not be held still while it was read)\n");
        if (suspended) thread_resume(port);
        tr_write(fd, buf, at);
    }
    at = rt_report_appendf(buf, sizeof buf, 0, "END OF THE WAITS\n");
    tr_write(fd, buf, at);
    rt_wait_report_unlock();
    if (list_locked) rt_thread_list_unlock();
}
