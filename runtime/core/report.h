/* Private to runtime/core: what the files of the crash unit share (public side: rt/crash.h,
 * and rt_log_raw / rt_log_flush_try in rt/log.h).
 *
 *     core/report.c      the one-report rule, the report text helpers       built
 *     core/crash.c       host signal handlers, alternate stacks             built
 *     core/exit.c        ExitProcess, TerminateProcess, FatalAppExit        stubs
 *     core/exception.c   the filter, RaiseException, DebugBreak             stubs
 *     core/excdecode.c   text for an exception code, record and context     stubs
 *
 * HOW A FATAL PATH IS WRITTEN.  Either build the whole block in a buffer of your own and
 * call rt_report_die(block), which does everything; or, to print in pieces:
 *
 *     if (rt_report_begin() == RT_REPORT_NESTED) rt_report_abort();
 *     size_t cap; char *buf = rt_report_buffer(&cap); size_t at = 0;
 *     at = rt_report_appendf(buf, cap, at, "FATAL ...\n");
 *     ...
 *     rt_log_flush_try();
 *     rt_log_raw(buf, at);
 *     rt_report_abort();
 *
 * Between rt_report_begin and the end the thread must take no lock and must not allocate:
 * no rt_log, no malloc, no rt_vm_query (rt_vm_probe), no handle-table call.  vsnprintf
 * into a buffer is allowed (Apple's C library takes no lock and allocates nothing for
 * %s %c %d %u %x %llx %p; floating-point conversions are forbidden, they may allocate).
 */
#ifndef RT_CORE_REPORT_H
#define RT_CORE_REPORT_H

#include "core.h"
#include "rt/crash.h"
#include "rt/vm.h"

/* ---- core/report.c: the one-report rule (rt/crash.h, "ONE REPORT") ---------------------------- */

typedef enum {
    RT_REPORT_FIRST = 0,      /* the caller owns the process's one report: print it and end the process */
    RT_REPORT_NESTED          /* the calling thread already owns it: a failure inside its own report */
} RtReportTurn;

/* Claim the process's one fatal report for the calling thread.
 *   RT_REPORT_FIRST    no thread had claimed it.  Also arms the watchdog: SIGALRM is set to
 *                      its default action and alarm(RT_REPORT_WATCHDOG_S) is called, so a
 *                      report that never finishes is killed.
 *   RT_REPORT_NESTED   the calling thread claimed it earlier.  The caller prints at most one
 *                      line with rt_log_raw and calls rt_report_abort.
 *   (does not return)  another thread owns it: the calling thread sleeps for ever in pause().
 * Async-signal-safe; takes no lock. */
RtReportTurn rt_report_begin(void);
#define RT_REPORT_WATCHDOG_S  10

/* True once any thread has claimed the report.  One atomic load. */
bool rt_report_active(void);

/* The buffer of the thread that owns the report: RT_REPORT_BUF bytes, static, so that a
 * report needs no stack to speak of (the handler runs on a small alternate stack).  Only
 * the thread to which rt_report_begin returned RT_REPORT_FIRST may use it. */
#define RT_REPORT_BUF  16384
char *rt_report_buffer(size_t *cap);

/* End the process after a report: every signal core/crash.c handles goes back to its
 * default action, then abort().  The process dies of SIGABRT; or, where rt_report_quiet
 * (rt/crash.h, THE QUIET END) asked for it and it applies to this report, SIGABRT gets a
 * handler that calls _exit(RT_REPORT_QUIET_STATUS) first, and the same abort() ends in that.
 * Async-signal-safe. */
_Noreturn void rt_report_abort(void);

/* core/crash.c calls this first thing in its signal handler: from now on the process's
 * report is, or has beside it, a host signal, which RT_QUIET_STOPS does not make quiet.
 * One atomic store. */
void rt_report_by_signal(void);

/* The whole of a simple fatal path: claim the report (on RT_REPORT_NESTED the block is
 * still printed), rt_log_flush_try, write `block` (NUL-terminated, should end in a newline)
 * with rt_log_raw, rt_report_abort.  Takes no lock: callable with any lock held. */
_Noreturn void rt_report_die(const char *block);

/* ---- core/report.c: text helpers.  Each appends at buf[at], never writes past buf[n-1],
 * always leaves buf NUL-terminated, and returns the new length (at most n-1).  None takes a
 * lock; all may be used inside a report and from a signal handler. ------------------------------ */

size_t rt_report_appendf(char *buf, size_t n, size_t at, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

/* Four lines, the sixteen registers in the order of Ctx.r[], each as 16 hex digits:
 *     <indent>rax=... rcx=... rdx=... rbx=...
 *     <indent>rsp=... rbp=... rsi=... rdi=...
 *     <indent>r8 =... r9 =... r10=... r11=...
 *     <indent>r12=... r13=... r14=... r15=...                                               */
size_t rt_report_regs(char *buf, size_t n, size_t at, const uint64_t r[16], const char *indent);

/* One line, "  address: <what>\n", describing a data address for a report: a null pointer,
 * a module (rt_module_describe), a VM region with its base, state and protection, a busy
 * map, or host memory.  `t` is the thread the report is about, or NULL: with it, the guard
 * area below its guest stack is named as a guest stack overflow. */
size_t rt_report_address(char *buf, size_t n, size_t at, uint64_t addr, const RtThread *t);

/* The guest side of a thread for a report: the registers (rt_report_regs), one line with
 * the flags, mxcsr and gs base, and one line per shim frame the thread is inside.  `t` NULL
 * prints "  (no guest thread)\n". */
size_t rt_report_guest(char *buf, size_t n, size_t at, const RtThread *t);

/* One line, "  inside the entry point of <module> (reason <n>)\n", if thread `t` is inside
 * a DllMain or a TLS callback the loader called (rt_loader_in_entry, rt/module.h: no lock,
 * safe in a signal handler on `t` itself; <n> is the DLL_* reason, 1 = DLL_PROCESS_ATTACH);
 * nothing otherwise, and nothing for t NULL.  rt_report_guest ends with it; the reports of
 * core/trap.c call it themselves. */
size_t rt_report_entry(char *buf, size_t n, size_t at, const RtThread *t);

/* True if every byte of [addr, addr + len) is guest memory that can be read now: inside an
 * arena, committed, and not PAGE_NOACCESS and not PAGE_GUARD, by rt_vm_probe on each guest
 * page of the range.  `len` 0 is treated as 1.  False for host memory (outside both
 * arenas), for a range that wraps, and when the map is busy.  The decoders below call it
 * before every read of a guest pointer they were handed, so that a bad pointer in an
 * exception record prints "not readable" instead of faulting inside the report.  Takes no
 * lock (rt_vm_probe). */
bool rt_report_readable(uint64_t addr, uint64_t len);

/* core/trap.c: rt_report_context (declared in core.h) appends the lines every non-signal
 * report shares: the guest caller, thread, argument registers and shim frames.  A core
 * function called from a shim passes the guest address that holds the return address of
 * the Windows call:
 *     RtThread *t = ctx ? RT_THREAD(ctx) : NULL;
 *     at += rt_report_context(buf + at, cap - at, ctx, t && t->shim ? t->shim->entry_rsp : 0);
 * (it returns the number of bytes it appended to the buffer it was given). */

/* ---- core/unwind.c: the guest call stack of a report (rt/unwind.h) ---------------------------- */

#define RT_REPORT_GUEST_FRAMES 16       /* frames printed; a longer stack ends "(more frames are not shown)" */

/* Append the guest call stack of the thread whose registers are `ctx`, one frame per line
 * as rt_module_describe writes it (module+rva; library!name for the return-to-host stub),
 * then one line in parentheses that says why the walk ended (rt/unwind.h lists the ends;
 * only "the stack is complete" is a whole stack):
 *     ret_addr_at != 0   "  guest stack (return addresses, innermost first):\n", the frames of
 *                        rt_unwind_backtrace_at: the thread is at a call boundary and
 *                        `ret_addr_at` holds the return address (a shim's entry_rsp)
 *     else body_fn != 0  "  guest stack, unwound from Ctx.rsp as if inside the body of <fn> (...):\n",
 *                        the frames of rt_unwind_backtrace_body: a host signal in lifted code,
 *                        where the function is known and the instruction is not
 *     both 0, or ctx NULL   "  guest stack: not available (no return address on the guest stack)\n"
 * Each frame line is "    #<i> <where>\n".  Takes no lock, reads guest memory only after
 * rt_report_readable: usable inside a report and from a signal handler. */
size_t rt_report_backtrace(char *buf, size_t n, size_t at, const Ctx *ctx, uint64_t ret_addr_at, uint64_t body_fn);

/* ---- core/crash.c ------------------------------------------------------------------------------- */

/* Set every signal the crash handler handles back to SIG_DFL.  Called by rt_report_abort.
 * Async-signal-safe. */
void rt_crash_handlers_off(void);

/* ---- core/vm.c: for the test of rt_vm_probe only ---------------------------------------------- */

/* Take (true) or release (false) the VM map's lock on the calling thread. */
void rt_vm_test_hold_lock(bool hold);

/* ---- core/excdecode.c: text for exceptions [not built].  Pure functions of their arguments
 * and of guest memory; every guest pointer is checked with rt_report_readable before it is
 * read; none takes a lock, logs, or is fatal.  Formats are printf formats; "\n" ends every
 * line. ------------------------------------------------------------------------------------------- */

/* The name of an exception code, or NULL if the code is not in this list:
 *   every EXCEPTION_* and STATUS_* constant of rt/win/exception.h: the constant's own name
 *       ("EXCEPTION_ACCESS_VIOLATION", "STATUS_STACK_BUFFER_OVERRUN", ...);
 *   DBG_PRINTEXCEPTION_C, DBG_PRINTEXCEPTION_WIDE_C: the constant's own name;
 *   RT_EXC_THREAD_NAME "thread name";  RT_EXC_CXX "C++ exception";
 *   RT_EXC_DELAYLOAD_MOD_NOT_FOUND "delay-load: module not found";
 *   RT_EXC_DELAYLOAD_PROC_NOT_FOUND "delay-load: procedure not found";
 *   RT_EXC_DELAYLOAD_INVALID_PARAMETER "delay-load: invalid parameter". */
const char *rt_exc_code_name(DWORD code);

/* Append what the parameters of an exception say.  `args` has `nargs` entries (nargs <= 15;
 * args may be NULL when nargs is 0).  Layouts are the Visual C++ x64 ones (ehdata.h,
 * delayimp.h); they are NOT VERIFIED against the game's DLLs, which is why every step is
 * checked and a mismatch prints "cannot be read" rather than a guess.
 *
 * code RT_EXC_CXX:
 *   nargs < 3, or (uint32_t)args[0] != RT_EXC_CXX_MAGIC:
 *       "  C++ exception with unexpected parameters\n"
 *   args[1] == 0 and args[2] == 0:
 *       "  C++ rethrow (throw;) with no exception object\n"
 *   otherwise, with B = (nargs >= 4 ? args[3] : 0) and all fields little-endian uint32_t:
 *       T  = args[2]                 ThrowInfo, 16 bytes;        rvaA = field at T + 12
 *       A  = B + rvaA                CatchableTypeArray, 8 bytes; count = field at A, rvaC = field at A + 4
 *       C  = B + rvaC                CatchableType, 8 bytes;      rvaD = field at C + 4
 *       D  = B + rvaD                TypeDescriptor;              the name starts at D + 16
 *     Each of T, A, C must pass rt_report_readable(x, its size) before it is read, and
 *     count must be >= 1 (as int32_t).  A STRING IN GUEST MEMORY (this name, and the two
 *     delay-load strings below) is read one byte at a time, each byte only after
 *     rt_report_readable(its address, 1); reading stops at a NUL, at the first byte that
 *     is not readable, or when 127 bytes have been read; the bytes read before that are
 *     the string.  The name must have at least one byte.  Success:
 *       "  C++ exception of type %s (object at 0x%llx)\n"     name as stored, args[1]
 *     Any check failing:
 *       "  C++ exception; the type cannot be read (ThrowInfo at 0x%llx)\n"     args[2]
 *
 * code RT_EXC_DELAYLOAD_MOD_NOT_FOUND, _PROC_NOT_FOUND, _INVALID_PARAMETER:
 *   I = args[0] is a DelayLoadInfo, 72 bytes: szDll (a guest pointer to a string) at I + 24,
 *   fImportByName (uint32_t) at I + 32, the procedure name pointer or the ordinal
 *   (uint32_t) at I + 40, dwLastError (uint32_t) at I + 64.
 *   nargs < 1 or rt_report_readable(I, 72) false:
 *       "  delay-load failure; the descriptor cannot be read\n"
 *   otherwise, with each string read by the rule above, and "?" in place of a string
 *   that comes out empty (a NULL pointer, an unreadable first byte, a first byte NUL):
 *       fImportByName != 0:  "  delay-load of %s!%s failed, last error %u\n"
 *       fImportByName == 0:  "  delay-load of %s!#%u failed, last error %u\n"
 *
 * code EXCEPTION_ACCESS_VIOLATION or EXCEPTION_IN_PAGE_ERROR, nargs >= 2:
 *       "  %s of address 0x%016llx\n"     "read" (args[0] == 0), "write" (1), "execute" (8),
 *                                          "access" (anything else); args[1]
 * Any other code, and the two above with nargs < 2: appends nothing. */
size_t rt_exc_describe(char *buf, size_t n, size_t at, DWORD code, DWORD nargs, const uint64_t *args);

/* Append the lines for the EXCEPTION_RECORD at guest address `record`:
 *   record == 0:                          "  no exception record\n"
 *   rt_report_readable(record, 152) false: "  exception record at 0x%llx is not readable guest memory\n"
 *   otherwise, with N = min(NumberParameters, 15):
 *       "  exception 0x%08x%s, flags 0x%x, at %s (0x%llx)\n"
 *             code; " (NAME)" if rt_exc_code_name knows it, else ""; ExceptionFlags;
 *             rt_module_describe(ExceptionAddress); ExceptionAddress
 *       "  %u parameters%s\n"     N; then ":" and " 0x%llx" for each of the N, or nothing if N is 0
 *       the lines of rt_exc_describe(code, N, ExceptionInformation)
 *       "  chained record at 0x%llx, not printed\n"     only if ExceptionRecord is not NULL */
size_t rt_exc_format_record(char *buf, size_t n, size_t at, uint64_t record);

/* Append the lines for the CONTEXT at guest address `context`:
 *   context == 0:                            "  no context record\n"
 *   rt_report_readable(context, 1232) false: "  context record at 0x%llx is not readable guest memory\n"
 *   otherwise:
 *       "  context: rip=%s (0x%016llx) eflags=%08x mxcsr=%08x\n"
 *             rt_module_describe(Rip); Rip; EFlags; MxCsr (the field at offset 0x34)
 *       the four lines of rt_report_regs(&Rax ... R15, indent "    ") */
size_t rt_exc_format_context(char *buf, size_t n, size_t at, uint64_t context);

#endif /* RT_CORE_REPORT_H */
