/* Process exit (docs/RUNTIME.md section 9.4).  Specification: rt/crash.h, "THE EXIT
 * SEQUENCE" and the four functions below it.  BUILT: every function below is written.
 *
 * State (declared now so that the body adds none):
 *   exit_claimed   0 until the first thread starts the exit sequence, then 1 for ever.
 *                  Claimed with one atomic exchange; no lock anywhere in this file.
 * Calls out of this file: rt_report_active, rt_report_begin, rt_report_appendf,
 * rt_report_buffer, rt_report_context (core/report.h, core/core.h); rt_log, rt_log_raw,
 * rt_log_flush_try (rt/log.h); rt_handle_ref, rt_object_unref (rt/handle.h);
 * rt_com_counts_print (rt/com.h: the call counts of the interface layer, before the flush;
 * it takes no lock); _exit, usleep, pause.  Nothing else.
 */
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

#include "report.h"
#include "rt/com.h"
#include "rt/handle.h"

static _Atomic int exit_claimed;

/* Steps 1 to 6 of the exit sequence.  `what` is "ExitProcess" or "TerminateProcess";
 * `detach_line` is true for ExitProcess (step 4). */
static _Noreturn void exit_sequence(Ctx *ctx, UINT code, const char *what, bool detach_line) {
    /* 1. Claim the exit: the first thread goes on, the others sleep for ever. */
    if (atomic_exchange(&exit_claimed, 1)) for (;;) pause();
    /* 2. A fatal report on another thread wins: sleep for ever, the report ends the process. */
    if (rt_report_active()) for (;;) pause();
    /* 3. One line on channel thread: INFO for code 0, WARN otherwise. */
    RtThread *t = ctx ? RT_THREAD(ctx) : rt_thread_self();
    RtLogLevel level = code == 0 ? RT_LL_INFO : RT_LL_WARN;
    if (t)
        rt_log(RT_CH_THREAD, level, "process exit: %s(0x%08x) on thread 0x%x \"%s\"",
               what, (unsigned)code, (unsigned)t->id, t->name);
    else
        rt_log(RT_CH_THREAD, level, "process exit: %s(0x%08x) on a host thread", what, (unsigned)code);
    /* 4. ExitProcess only: no DLL_PROCESS_DETACH is delivered before Gate 5 (section 11.1). */
    if (detach_line)
        rt_log(RT_CH_LOADER, RT_LL_INFO, "ExitProcess: DLL_PROCESS_DETACH is not delivered (RUNTIME.md 11.1)");
    /* The calls through interface slots so far (rt/com.h); nothing when there were none. */
    rt_com_counts_print();
    /* 5. Flush: rt_log_flush_try up to 200 times, 1 ms apart, until it returns true. */
    for (int tries = 0; !rt_log_flush_try() && tries < 199; tries++) usleep(1000);
    /* 6. The host keeps 8 bits of the status: a code above 254 becomes 255. */
    _exit(code <= 254 ? (int)code : 255);
}

_Noreturn void rt_process_exit(Ctx *ctx, UINT code) {
    exit_sequence(ctx, code, "ExitProcess", true);
}

/* True when `process` is the current process: (HANDLE)-1 is tested before the handle table
 * is asked; any other value must resolve to the one RT_K_PROCESS object (reference dropped).
 * A failed lookup has set ERROR_INVALID_HANDLE (rt_handle_ref). */
static bool is_current_process(HANDLE process) {
    if (process == (HANDLE)-1) return true;
    RtObject *obj = rt_handle_ref(process, RT_K_PROCESS);
    if (!obj) return false;
    rt_object_unref(obj);
    return true;
}

BOOL rt_process_terminate(Ctx *ctx, HANDLE process, UINT code) {
    if (!is_current_process(process)) return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
    exit_sequence(ctx, code, "TerminateProcess", false);
}

BOOL rt_process_exit_code(Ctx *ctx, HANDLE process, DWORD *code) {
    if (!is_current_process(process)) return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
    if (!code) RT_UNIMPLEMENTED(ctx, "GetExitCodeProcess with lpExitCode NULL");
    *code = STILL_ACTIVE;               /* the current process has not ended */
    return TRUE;
}

_Noreturn void rt_fatal_app_exit(Ctx *ctx, UINT action, const char *message) {
    if (rt_report_begin() == RT_REPORT_NESTED) rt_report_abort();
    size_t cap;
    char *buf = rt_report_buffer(&cap);
    size_t at = 0;
    /* The message: its bytes up to the first NUL, at most 1,000 (the precision of %s). */
    if (message)
        at = rt_report_appendf(buf, cap, at, "FATAL guest called FatalAppExit: %.1000s\n", message);
    else
        at = rt_report_appendf(buf, cap, at, "FATAL guest called FatalAppExit: (null)\n");
    at = rt_report_appendf(buf, cap, at, "  uAction 0x%x\n", (unsigned)action);
    RtThread *t = ctx ? RT_THREAD(ctx) : NULL;
    at += rt_report_context(buf + at, cap - at, ctx, t && t->shim ? t->shim->entry_rsp : 0);
    rt_log_flush_try();
    rt_log_raw(buf, at);
    _exit(3);
}
