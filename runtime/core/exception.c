/* Guest exceptions, plan A (docs/RUNTIME.md section 9.3).  Specification: rt/crash.h,
 * "exceptions, plan A".  BUILT: the filter, UnhandledExceptionFilter, RaiseException and
 * DebugBreak.
 *
 * State (declared now so that the body adds none):
 *   top_filter   the guest's top-level exception filter, 0 at start.  Read and written
 *                with atomic operations only; no lock anywhere in this file.
 * Calls out of this file: rt_report_begin, rt_report_abort, rt_report_buffer,
 * rt_report_appendf, rt_report_readable, rt_report_context, rt_exc_code_name,
 * rt_exc_describe, rt_exc_format_record, rt_exc_format_context (core/report.h,
 * core/core.h); rt_log, rt_log_raw, rt_log_flush_try (rt/log.h); rt_thread_set_name
 * (rt/thread.h).  Nothing else.
 */
#include <stdatomic.h>
#include <string.h>

#include "report.h"

static _Atomic uint64_t top_filter;

/* Ends a report that was built in the report buffer: the log gets the lines, the process ends. */
static _Noreturn void report_end(char *buf, size_t at) {
    rt_log_flush_try();
    rt_log_raw(buf, at);
    rt_report_abort();
}

/* Appends the rt_report_context lines for `ctx` at buf[at]; returns the new length. */
static size_t context_lines(char *buf, size_t cap, size_t at, Ctx *ctx) {
    RtThread *t = ctx ? RT_THREAD(ctx) : NULL;
    return at + rt_report_context(buf + at, cap - at, ctx, t && t->shim ? t->shim->entry_rsp : 0);
}

GUESTFN rt_uef_set(GUESTFN filter) {
    return (GUESTFN)atomic_exchange(&top_filter, (uint64_t)filter);
}

GUESTFN rt_uef_get(void) {
    return (GUESTFN)atomic_load(&top_filter);
}

_Noreturn void rt_uef_unhandled(Ctx *ctx, uint64_t info) {
    if (rt_report_begin() == RT_REPORT_NESTED) rt_report_abort();
    size_t cap;
    char *buf = rt_report_buffer(&cap);
    size_t at = rt_report_appendf(buf, cap, 0, "FATAL guest called UnhandledExceptionFilter\n");
    if (info == 0) {
        at = rt_report_appendf(buf, cap, at, "  no EXCEPTION_POINTERS\n");
    } else if (!rt_report_readable(info, 16)) {
        at = rt_report_appendf(buf, cap, at, "  EXCEPTION_POINTERS at 0x%llx is not readable guest memory\n",
                               (unsigned long long)info);
    } else {
        at = rt_exc_format_record(buf, cap, at, LD64(info));
        at = rt_exc_format_context(buf, cap, at, LD64(info + 8));
    }
    at = rt_report_appendf(buf, cap, at, "  top-level filter 0x%016llx, not called (RUNTIME.md 9.3)\n",
                           (unsigned long long)rt_uef_get());
    at = context_lines(buf, cap, at, ctx);
    report_end(buf, at);
}

/* RT_EXC_THREAD_NAME: names the calling thread, or logs that it cannot name another one. */
static void thread_name(Ctx *ctx, DWORD nargs, const ULONG_PTR *args) {
    if (nargs < 3 || (uint32_t)args[0] != 0x1000 || args[1] == 0) {
        rt_log(RT_CH_THREAD, RT_LL_WARN, "malformed thread-name exception ignored");
        return;
    }
    const char *name = (const char *)(uintptr_t)args[1];
    DWORD id = (DWORD)args[2];
    RtThread *t = RT_THREAD(ctx);
    if (id == 0xFFFFFFFF || id == t->id) {
        rt_thread_set_name(t, name);
    } else {
        rt_log(RT_CH_THREAD, RT_LL_WARN,
               "thread name \"%s\" for thread 0x%x ignored: naming another thread is not built",
               name, (unsigned)id);
    }
}

/* DBG_PRINTEXCEPTION_C and _WIDE_C: one INFO line on guest.debug.  The text is read up to its
 * first NUL and at most min(limit, 1000) bytes; then one trailing "\n", and then one "\r". */
static void debug_text(uint64_t limit, uint64_t text) {
    uint64_t max = limit < 1000 ? limit : 1000;
    uint64_t n = 0;
    while (n < max && LD8(text + n) != 0) n++;
    if (n > 0 && LD8(text + n - 1) == '\n') n--;
    if (n > 0 && LD8(text + n - 1) == '\r') n--;
    rt_log(RT_CH_GUEST_DEBUG, RT_LL_INFO, "%.*s", (int)n, (const char *)(uintptr_t)text);
}

/* Every other code: the one report.  Never returns. */
static _Noreturn void raise_report(Ctx *ctx, DWORD code, DWORD flags, DWORD nargs, const ULONG_PTR *args) {
    if (rt_report_begin() == RT_REPORT_NESTED) rt_report_abort();
    size_t cap;
    char *buf = rt_report_buffer(&cap);
    size_t at = rt_report_appendf(buf, cap, 0, "FATAL guest raised exception 0x%08x", (unsigned)code);
    const char *name = rt_exc_code_name(code);
    if (name) at = rt_report_appendf(buf, cap, at, " (%s)", name);
    at = rt_report_appendf(buf, cap, at, "\n  flags 0x%x, %u parameters", (unsigned)flags, (unsigned)nargs);
    if (nargs > 0) {
        at = rt_report_appendf(buf, cap, at, ":");
        for (DWORD i = 0; i < nargs; i++)
            at = rt_report_appendf(buf, cap, at, " 0x%llx", (unsigned long long)args[i]);
    }
    at = rt_report_appendf(buf, cap, at, "\n");
    at = rt_exc_describe(buf, cap, at, code, nargs, args);
    at = context_lines(buf, cap, at, ctx);
    report_end(buf, at);
}

void rt_raise_exception(Ctx *ctx, DWORD code, DWORD flags, DWORD nargs, const ULONG_PTR *args) {
    if (nargs > 15) nargs = 15;
    if (!args) nargs = 0;
    if (code == RT_EXC_THREAD_NAME) {
        thread_name(ctx, nargs, args);
    } else if (code == DBG_PRINTEXCEPTION_C) {
        if (nargs >= 2 && args[1] != 0) debug_text(args[0], args[1]);
    } else if (code == DBG_PRINTEXCEPTION_WIDE_C) {
        if (nargs >= 4 && args[3] != 0) debug_text(args[2], args[3]);
    } else {
        raise_report(ctx, code, flags, nargs, args);
    }
}

_Noreturn void rt_debug_break(Ctx *ctx) {
    if (rt_report_begin() == RT_REPORT_NESTED) rt_report_abort();
    size_t cap;
    char *buf = rt_report_buffer(&cap);
    size_t at = rt_report_appendf(buf, cap, 0, "FATAL guest called DebugBreak\n");
    at = context_lines(buf, cap, at, ctx);
    report_end(buf, at);
}
