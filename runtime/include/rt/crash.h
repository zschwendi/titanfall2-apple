/* Crash reporting, guest exceptions and process exit (docs/RUNTIME.md sections 9.3, 9.4).
 *
 *     Windows function              core function            file
 *     ExitProcess                   rt_process_exit          core/exit.c        [not built]
 *     TerminateProcess              rt_process_terminate     core/exit.c        [not built]
 *     GetExitCodeProcess            rt_process_exit_code     core/exit.c        [not built]
 *     FatalAppExitA                 rt_fatal_app_exit        core/exit.c        [not built]
 *     SetUnhandledExceptionFilter   rt_uef_set               core/exception.c   [not built]
 *     UnhandledExceptionFilter      rt_uef_unhandled         core/exception.c   [not built]
 *     RaiseException                rt_raise_exception       core/exception.c   [not built]
 *     DebugBreak                    rt_debug_break           core/exception.c   [not built]
 *     (a host signal)               rt_crash_init            core/crash.c       built
 * IsDebuggerPresent and OutputDebugStringA/W need nothing from here (they are shims over
 * rt_log).  RtlCaptureContext, RtlLookupFunctionEntry, RtlVirtualUnwind and RtlPcToFileHeader
 * belong to the unwinder (rt/unwind.h, runtime/core/unwind.c: built); RtlUnwindEx stays a stop.
 *
 * A function marked [not built] calls RT_CORE_UNIMPLEMENTED; its comment here is what its
 * body must do.  docs/handoff/rt-contract-crash.md has the test plan of each.
 *
 * PLAN A (section 9.3).  No guest exception handler ever runs and no guest filter is ever
 * called.  Everything that would start exception dispatch on Windows ends this process with
 * a fatal report instead; the three RaiseException codes that are messages to a debugger
 * return.  RT_CALL_UEF=1 (call the guest's filter from a fatal report) is NOT BUILT:
 * rt_crash_init is fatal if RT_CALL_UEF is set to anything but "" or "0".
 *
 * ONE REPORT.  A process prints at most one fatal report, whichever way it fails: rt_fatal
 * and its relatives (rt/log.h), a host signal, an exception, FatalAppExit.  The first thread
 * that fails owns the report; a thread that fails while another owns it prints nothing and
 * sleeps until the owner ends the process (it never returns to guest code); a second
 * failure on the owning thread prints one line and aborts.  The report is written with
 * rt_log_raw and takes NO LOCK: not the log's, not the VM map's (rt_vm_probe), not the
 * thread list's, and it does not allocate.  So every function here that ends in a report
 * may be called with any lock held.  A watchdog (alarm, 10 seconds) kills a report that
 * hangs all the same.  The reports end in abort(): the process dies of SIGABRT, unless the
 * program asked for the quiet end (rt_report_quiet, below).
 *
 * LAST ERROR.  Convention 1 of runtime/README.md: a function here that fails sets the
 * calling thread's last error itself and leaves it alone on success; the shim returns the
 * result and sets nothing.  Only rt_process_terminate and rt_process_exit_code can fail.
 *
 * LOCKS.  None of these functions takes a lock that it holds across a call out of this
 * unit, and none may be called from an RtObjectOps callback or with the dispatcher lock
 * held, except the fatal ones (see ONE REPORT).  rt_uef_set is one atomic exchange.
 */
#ifndef RT_CRASH_H
#define RT_CRASH_H

#include "runtime.h"
#include "rt/wintypes.h"
#include "rt/win/exception.h"

/* ---- host signals (built: runtime/core/crash.c) ---------------------------------------------- */

/* Called once by rt_init, on the main host thread, right after rt_vm_init (rt/init.h).
 * Installs one handler for SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGFPE and SIGABRT, on the
 * alternate signal stack, and gives the calling thread its alternate stack
 * (rt_crash_thread_attach).  Fatal if RT_CALL_UEF is set (see PLAN A) or if a handler
 * cannot be installed.
 *
 * The handler never returns to the code that faulted.  It prints the report below and
 * aborts.  What the report says, line by line (a sample is in the handoff):
 *     FATAL host signal SIGSEGV (11), code 2, fault address 0x...
 *       address: what rt_vm_probe and the module registry say about the fault address:
 *                "null pointer", module+rva, VM region with base, state and protection,
 *                "the VM map is busy", "host memory"; and, when it applies, "guest stack
 *                overflow" (the guard area below the thread's guest stack) or "host stack
 *                overflow" (just below the thread's host stack)
 *       thread 0x1004 "name", and RtThread.in_host: inside a shim, or in lifted code (or host
 *                code outside a shim); or "not a guest thread"
 *       host pc, lr, sp, fp, and pc as an offset into the host program's image
 *       nearest dispatch entry below pc: module+rva (or library!Name) and the distance.  A
 *                hint only, written without any lock: the function that really contains pc
 *                is frame #0 of the host frames
 *       the sixteen guest registers, the flags, mxcsr and gs base, as last stored in Ctx
 *       in shim lib.dll!Name, called from module+rva        (one line per shim frame)
 *       guest stack: the guest callers, one `#n module+rva` line each (rt_report_backtrace,
 *                core/report.h).  Inside a shim it comes here and is exact: it starts at the
 *                return address of the shim's call.  In lifted code it comes after the host
 *                frames, because only they say which lifted function the thread was in, and
 *                it is unwound as if the thread were in that function's body (the line says so)
 *       host frames: #n pc, then `module+rva  [host symbol+offset]` when the frame is a
 *                lifted function, a shim thunk or a trap stub (its host function is a
 *                dispatch entry), else `host symbol+offset (image)`
 * SIGABRT is handled so that an abort() made by the C library or another host library
 * (heap corruption, an assertion) gets the same report, headed "FATAL host abort()"; the
 * abort that ends one of our own reports is not reported again.
 *
 * NOT IN THE REPORT, and why.  The guest registers are the values lifted code last stored
 * in Ctx: the C compiler keeps some in host registers between stores, so a register may be
 * older than the fault (the report says so).  The guest instruction that faulted is not
 * known, only the lifted function: so the guest stack of a fault in lifted code is a guess
 * where the fault was in a prologue or an epilogue, and the host frames, which are exact
 * (a lifted call is a host call), stay in the report beside it.
 * The last block (host frames with symbols) calls dladdr, which may take the dynamic
 * linker's lock; it is printed after everything else has been written, and the watchdog
 * covers it. */
void rt_crash_init(void);

/* Give the calling host thread an alternate signal stack (128 KB, mapped here), so that a
 * guest or host stack overflow on it can be reported.  FOR THE THREAD UNIT: call it on
 * every new host thread before the thread runs guest code (rt_thread_create's start
 * routine, rt_thread_attach), and rt_crash_thread_detach as the thread's last runtime call
 * (rt_thread_exit's base frame, rt_thread_detach).  Idempotent; takes no lock; fatal if the
 * stack cannot be mapped.  A host thread that never calls it is still reported on a fault,
 * on its own stack, and so cannot report its own stack overflow.
 * After fork: macOS does not carry the setting into the child (measured), so rt_crash_init
 * registers a pthread_atfork handler that sets the forking thread's stack again there. */
void rt_crash_thread_attach(void);

/* Undo rt_crash_thread_attach for the calling thread: the alternate stack is disabled and
 * unmapped.  Does nothing if the thread has none.  Must not be called from a signal
 * handler. */
void rt_crash_thread_detach(void);

/* ---- how a fatal report ends the process (runtime/core/report.c) ------------------------------ */

/* THE QUIET END.  A process that dies of SIGABRT is written up by macOS's crash reporter
 * (a file in ~/Library/Logs/DiagnosticReports, and a second or so of three system services).
 * That is right for a crash and a waste for a stop that was expected and has already printed
 * its report: the 2,100 children of the test suite that exist to stop, a run of the game that
 * ends at the next thing not built.  With the quiet end the report is the same, to the byte
 * and in the same order, and so is everything abort() does before it raises the signal (the
 * C library's streams are flushed); only the last step differs: SIGABRT finds a handler that
 * calls _exit(RT_REPORT_QUIET_STATUS), so the process EXITS with status 134 (128 + SIGABRT,
 * what a shell shows for a process that SIGABRT killed) and no crash report is written.
 *
 * The switch is this function and nothing else: no environment variable, so nothing a
 * program starts can inherit it.  It belongs to the process (a forked child has its
 * parent's setting) and may be called at any time, before rt_init too.
 *   RT_QUIET_OFF     every report ends in abort() and the process dies of SIGABRT.  The default.
 *   RT_QUIET_STOPS   a report that is a stop ends quietly: rt_fatal and its relatives
 *                    (rt/log.h), an unimplemented import, a guest exception, FatalAppExit.
 *                    A report the signal handler wrote (a host fault, an abort() of a host
 *                    library: rt_crash_init) still ends in SIGABRT, and so does a stop during
 *                    which any thread took such a signal: that is a crash, of the game's code
 *                    or of ours, and stays as loud as without the switch.  For a driver's run.
 *   RT_QUIET_ALL     every report ends quietly, the signal handler's too.  For a process in
 *                    which the fault is the experiment: a child a test forked.
 * What stays loud under every setting: a signal the runtime does not handle, a fault after
 * the report's owner has set the handlers back (crash.c, "a third failure"), the watchdog. */
typedef enum { RT_QUIET_OFF = 0, RT_QUIET_STOPS, RT_QUIET_ALL } RtReportQuiet;
#define RT_REPORT_QUIET_STATUS 134
void rt_report_quiet(RtReportQuiet q);

/* ---- process exit (section 9.4; runtime/core/exit.c) ------------------------------------------ */

/* THE EXIT SEQUENCE, shared by rt_process_exit and rt_process_terminate.  `what` is
 * "ExitProcess" or "TerminateProcess".
 *   1. Claim the exit.  The first thread to arrive goes on; any other thread that calls
 *      either function afterwards sleeps for ever (on Windows the first caller's exit
 *      terminates it; either way it never returns).
 *   2. If a fatal report is in progress on another thread, sleep for ever: the abort wins,
 *      so a failing run never ends with an exit status that looks like a clean exit.
 *   3. Log one line on channel `thread`, level INFO if `code` is 0 and WARN otherwise:
 *          process exit: <what>(0x<code, 8 hex digits>) on thread 0x<id> "<name>"
 *      The thread is RT_THREAD(ctx), or rt_thread_self() when ctx is NULL; if that is
 *      NULL too, the line ends `on a host thread` in place of `on thread ...`.
 *   4. rt_process_exit only: log one line on channel `loader`, level INFO, exactly
 *          ExitProcess: DLL_PROCESS_DETACH is not delivered (RUNTIME.md 11.1)
 *      WHERE THIS MEETS THE LOADER.  Windows' ExitProcess ends every other thread and then
 *      calls each loaded module's entry point with DLL_PROCESS_DETACH (lpReserved not NULL)
 *      on the exiting thread; TerminateProcess calls nothing.  This runtime delivers no
 *      detach before Gate 5 (section 11.1), so the two functions differ only in the lines
 *      they log.  When detach is built it goes here, in this order: the thread unit stops
 *      the other guest threads, then the loader runs the detach calls in reverse load
 *      order.  Neither function exists yet; nothing is called.
 *   5. Flush the log without waiting for ever: call rt_log_flush_try up to 200 times, 1 ms
 *      apart (usleep), until it returns true.
 *   6. _exit(status), where status is `code` if code <= 254 and 255 otherwise.  The host
 *      keeps only 8 bits of an exit status: a code like 0xC0000409 or 0x100 must not turn
 *      into a status that reads as success, and the full code is in the line of step 3.
 * Other guest threads are not stopped: they run until _exit ends them.  No guest code runs
 * during the sequence, no atexit handler, no FLS callback; open files are closed by the
 * host kernel; data in the guest C runtime's stdio buffers is lost (section 11.1). */

/* ExitProcess(uExitCode).  Runs the exit sequence.  Never returns, never fails.  `ctx` may
 * be NULL (a host thread, or before guest thread 1 exists). */
_Noreturn void rt_process_exit(Ctx *ctx, UINT code);

/* TerminateProcess(hProcess, uExitCode).
 *   process   (HANDLE)-1, the pseudo-handle of the current process: runs the exit sequence
 *             and does not return.  This value is tested BEFORE the handle table is asked,
 *             so it works even when no process object is registered (rt/handle.h): tier0's
 *             Plat_ExitProcess and the C runtime's failure paths call exactly
 *             TerminateProcess(GetCurrentProcess(), code).
 *             Any other value is looked up with rt_handle_ref(process, RT_K_PROCESS).  If
 *             that finds an object, it is the current process (the only process object
 *             there is: child processes are not built): drop the reference and run the
 *             exit sequence.
 * Returns FALSE with last error ERROR_INVALID_HANDLE (set by rt_handle_ref: NULL, a handle
 * that is not open, a handle of another kind).  Never returns TRUE. */
BOOL rt_process_terminate(Ctx *ctx, HANDLE process, UINT code);

/* GetExitCodeProcess(hProcess, lpExitCode).  `process` is resolved exactly as in
 * rt_process_terminate ((HANDLE)-1 first, then rt_handle_ref with RT_K_PROCESS, reference
 * dropped).  The current process has not ended: *code = STILL_ACTIVE (259), returns TRUE.
 * FALSE with ERROR_INVALID_HANDLE as above; *code is then not written.
 * `code` NULL with a valid handle: not verified what Windows does (the kernel call fails
 * or the write faults); RT_UNIMPLEMENTED(ctx, "GetExitCodeProcess with lpExitCode NULL"). */
BOOL rt_process_exit_code(Ctx *ctx, HANDLE process, DWORD *code);

/* FatalAppExitA(uAction, lpMessageText): Windows shows the text in a message box and ends
 * the process when the box is closed.  Here: a fatal report
 *     FATAL guest called FatalAppExit: <message>
 *       uAction 0x<action>
 * followed by the lines of rt_report_context (the guest caller, thread, registers), then
 * the log is flushed (rt_log_flush_try, once) and the process ends with _exit(3), not with
 * abort (section 9.4; the exit status Windows gives is not verified).
 *   action    documented as "must be zero"; printed, never checked.
 *   message   bytes in code page 1252, printed as they are up to the first NUL, at most
 *             1,000 bytes; NULL prints `(null)`.
 * Obeys ONE REPORT (rt_report_begin).  Never returns. */
_Noreturn void rt_fatal_app_exit(Ctx *ctx, UINT action, const char *message);

/* ---- exceptions, plan A (section 9.3; runtime/core/exception.c) -------------------------------- */

/* SetUnhandledExceptionFilter(lpTopLevelExceptionFilter): store `filter` as the process's
 * top-level filter and return the one it replaces (0 the first time).  Any value is
 * stored, 0 included (the C runtime passes 0 before it calls UnhandledExceptionFilter).
 * One atomic exchange: no lock, no last error, cannot fail, callable from any thread.  The
 * stored filter is never called (PLAN A). */
GUESTFN rt_uef_set(GUESTFN filter);

/* The filter rt_uef_set stored last, or 0.  For reports. */
GUESTFN rt_uef_get(void);

/* UnhandledExceptionFilter(ExceptionInfo): the caller has decided to die and asks Windows
 * to show the crash dialog.  Windows returns EXCEPTION_EXECUTE_HANDLER or
 * EXCEPTION_CONTINUE_SEARCH; this function never returns.  It prints
 *     FATAL guest called UnhandledExceptionFilter
 * then the lines of rt_exc_format_record for info->ExceptionRecord and of
 * rt_exc_format_context for info->ContextRecord (core/report.h gives both formats and
 * what they print for a pointer that cannot be read), then
 *       top-level filter 0x<rt_uef_get(), 16 hex digits>, not called (RUNTIME.md 9.3)
 * then the lines of rt_report_context, and aborts.
 *   info   a guest address; 0 prints `  no EXCEPTION_POINTERS` in place of the record and
 *          context lines.  It is read only after rt_report_readable(info, 16) says yes;
 *          otherwise `  EXCEPTION_POINTERS at 0x<info> is not readable guest memory`.
 * Obeys ONE REPORT. */
_Noreturn void rt_uef_unhandled(Ctx *ctx, uint64_t info);

/* RaiseException(dwExceptionCode, dwExceptionFlags, nNumberOfArguments, lpArguments).
 * `nargs` above 15 is used as 15; `args` NULL makes nargs 0 (both as Windows documents).
 * RETURNS, having done what a debugger that continues the exception would have done, for:
 *   RT_EXC_THREAD_NAME (0x406D1388)   if nargs >= 3, the low 32 bits of args[0] are 0x1000
 *       and args[1] is not 0: the name is the NUL-terminated code page 1252 string at
 *       args[1] and the thread is the low 32 bits of args[2].  If that is 0xFFFFFFFF or the
 *       calling thread's id, rt_thread_set_name(RT_THREAD(ctx), name).  Another thread's
 *       id: one line, channel `thread`, level WARN: `thread name "<name>" for thread
 *       0x<id> ignored: naming another thread is not built` (it needs a lookup by id from
 *       the thread unit).  Any other shape of arguments: one WARN line on `thread`,
 *       `malformed thread-name exception ignored`.
 *   DBG_PRINTEXCEPTION_C (0x40010006)   if nargs >= 2 and args[1] is not 0: one line on
 *       channel `guest.debug`, level INFO: the bytes at args[1] up to the first NUL, but no
 *       more than min(args[0], 1000) bytes; then, if the text ends in '\n', that byte is
 *       dropped, and if it then ends in '\r', that one too.  (Windows passes the length
 *       with the NUL counted in args[0].)
 *   DBG_PRINTEXCEPTION_WIDE_C (0x4001000A)   if nargs >= 4 and args[3] is not 0: the same
 *       with the ANSI copy that Windows passes as args[2] (length) and args[3] (text).
 *       For both print codes any other shape logs nothing and returns.
 * EVERY OTHER CODE never returns.  It prints
 *     FATAL guest raised exception 0x<code, 8 hex digits><" (NAME)" if rt_exc_code_name knows it>
 *       flags 0x<flags>, <nargs> parameters<": " and the parameters as 0x%llx separated by one space, if nargs > 0>
 * then the lines of rt_exc_describe (the C++ type, the delay-load DLL and function, the
 * access-violation address; nothing for other codes), then the lines of rt_report_context
 * with the guest return address of the RaiseException call, and aborts.  Obeys ONE REPORT.
 * `flags` is only printed.  Last error is never touched.  `ctx` may be NULL only for a code
 * that ends in the report (it is then printed as "no guest thread"); the three codes that
 * return need the calling guest thread. */
void rt_raise_exception(Ctx *ctx, DWORD code, DWORD flags, DWORD nargs, const ULONG_PTR *args);

/* DebugBreak(): prints `FATAL guest called DebugBreak` and the lines of rt_report_context,
 * and aborts (section 9.4: no debugger is ever attached).  A guest int3 instruction does
 * not come here: the lifter turns it into TRAP, which is rt_trap.  Obeys ONE REPORT. */
_Noreturn void rt_debug_break(Ctx *ctx);

#endif /* RT_CRASH_H */
