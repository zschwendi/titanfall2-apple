/* Guest threads and the process (docs/RUNTIME.md sections 6.1 to 6.3, 4.1, 4.2).
 *
 * One host thread per guest thread.  Each has an RtThread in host memory, which embeds the
 * thread's Ctx; ctx.host points back at it, so RT_THREAD(ctx) is free.  The guest-visible
 * part of a thread (thread block, guest stack, TLS blocks) lives in the VM arena and is
 * described in rt/win/teb.h.
 *
 * FILES.  core/thread.c: the process block, guest thread 1, the list of running threads, the
 * accessors.  core/thread_run.c: CreateThread, the start trampoline, ExitThread, ResumeThread
 * and the thread object.  Both are built, and so is core/thread_com.c (the COM counts of a
 * thread).  core/thread_tls.c, thread_fls.c, thread_ctl.c,
 * thread_attach.c and process.c hold the rest: until each is built its functions stop with
 * RT_CORE_UNIMPLEMENTED (docs/handoff/rt-contract-thread.md lists what is built).
 *
 * WHICH FUNCTION A SHIM CALLS.  Each shim of this family is one call.
 *
 *     CreateThread         rt_thread_create           TlsAlloc            rt_tls_alloc
 *     ExitThread           rt_thread_exit             TlsFree             rt_tls_free
 *     ResumeThread         rt_thread_resume           TlsGetValue         rt_tls_get
 *     GetExitCodeThread    rt_thread_get_exit_code    TlsSetValue         rt_tls_set
 *     SetThreadPriority    rt_thread_set_priority     FlsAlloc            rt_fls_alloc
 *     GetThreadPriority    rt_thread_get_priority     FlsFree             rt_fls_free
 *     SetThreadAffinityMask  rt_thread_set_affinity   FlsGetValue         rt_fls_get
 *     OpenThread           rt_thread_open             FlsSetValue         rt_fls_set
 *     ExitProcess, TerminateProcess, GetExitCodeProcess: rt/crash.h
 *     SetPriorityClass     rt_process_set_priority_class
 *     GetProcessAffinityMask  rt_process_get_affinity GetPriorityClass    rt_process_get_priority_class
 *     SetProcessAffinityMask  rt_process_set_affinity QueueUserAPC        rt_thread_from_handle, rt_apc_queue
 *     SetErrorMode         rt_process_swap_error_mode GetProcessTimes     rt_process_times
 *     SetThreadExecutionState  rt_thread_execution_state
 *     CoInitializeEx, CoInitialize  rt_com_initialize CoUninitialize      rt_com_uninitialize
 *     OleInitialize        rt_ole_initialize
 *
 * SuspendThread, TerminateThread, GetThreadContext and SetThreadContext have no core
 * function: each is a shim of status `trap` (section 6.3) until a caller is seen.
 * Parameters these functions do not take are the shim's to ignore: security attributes, the
 * desired access and the inherit flag.
 *
 * LAST ERROR (section 3.4; the table in runtime/README.md, row thread.h).  A function here
 * that fails sets the calling thread's last error itself and says which codes; none touches
 * it on success, with two exceptions: rt_tls_get and rt_fls_get set ERROR_SUCCESS when they
 * succeed (rt_tls_get as TlsGetValue is documented to, rt_fls_get as the Wine test in its
 * header requires).  A shim returns the result and sets nothing.
 *
 * LOCKS.  Two, never held together:
 *   - The THREAD LOCK, a host os_unfair_lock in core/thread.c (core/thread_priv.h declares
 *     it).  It guards the list of running guest threads (RtThread.next), their count, and the
 *     TLS and FLS index bitmaps with the FLS callbacks.  It is not recursive.  While holding
 *     it a thread may read and write host and guest memory and allocate (malloc, rt_galloc,
 *     rt_vm_alloc); it may NOT make a guest call, wait or sleep, log, take the dispatcher
 *     lock or the loader's lock, or create, exit or look up a thread.
 *   - The DISPATCHER LOCK (rt/handle.h) guards every field of a thread object and of the
 *     process object that changes after creation: exited, the exit code, the recorded
 *     priority and affinity.  The rules of rt/handle.h apply.
 * The loader's lock, where the loader holds it across a call into this header, is taken
 * BEFORE the thread lock, never after.  Every function below takes the locks it needs
 * itself: call them with neither lock held unless a comment says otherwise.
 *
 * WHAT IS NOT VERIFIED on Windows is marked "not verified" where it is specified; in each
 * such place the runtime stops with a report instead of choosing an answer.
 */
#ifndef RT_THREAD_H
#define RT_THREAD_H

#include "runtime.h"            /* Ctx: the lifter's contract (harness/runtime.h) */
#include "rt/wintypes.h"
#include "rt/handle.h"
#include "rt/win/base.h"        /* FILETIME */
#include "rt/win/com.h"         /* COINIT_* */
#include "rt/win/process.h"     /* CREATE_SUSPENDED, STILL_ACTIVE, THREAD_PRIORITY_*, *_PRIORITY_CLASS, ES_*, SEM_* */

struct RtShimFrame;

/* Per-thread state that belongs to one subsystem.  Each subsystem owns exactly one slot of
 * RtThread.sub[], keeps there a pointer to whatever it needs per thread (the wait core: the
 * thread's condition variable and wait record), allocates it on first use and frees it in
 * its thread-exit hook.  This is how a subsystem gets per-thread state without editing this
 * header. */
typedef enum {
    RT_SUB_WAIT = 0,      /* wait core: condition variable, wait record, APC queue */
    RT_SUB_HANDLE,        /* handle table: the thread's own thread object (the rule is below) */
    RT_SUB_LOADER,        /* loader: static TLS block bookkeeping */
    RT_SUB_THREAD,        /* thread unit: RtThreadPriv (core/thread_priv.h); set for every guest thread */
    RT_SUB_FILE,          /* files: per-thread overlapped state */
    RT_SUB_EXCEPTION,     /* exceptions and unwinder */
    RT_SUB_TEST,          /* the test harness */
    RT_SUB_SPARE,
    RT_SUB_COUNT
} RtSubsystem;

/* RT_SUB_HANDLE is the one slot whose reader and writer are different units.
 *
 * It holds a pointer to the thread's own RT_K_THREAD object (rt/handle.h), or NULL.  The
 * handle table only READS it, and only on the thread it belongs to: rt_handle_ref and
 * rt_handle_dup (and so every function that takes a handle) resolve the pseudo-handle
 * (HANDLE)-2 through rt_thread_self()->sub[RT_SUB_HANDLE] and take their own reference to
 * what they find.  No
 * lock guards the slot, because no other thread reads or writes it.
 *
 * The THREAD UNIT writes it, by these rules:
 *   - It makes the thread object, keeps one reference to it for as long as the thread runs,
 *     and stores the object's address in the slot before the thread runs any guest code
 *     (for guest thread 1: in rt_thread_attach_main; the handle table exists by then).
 *   - The pointer in the slot is BORROWED: the slot is not a reference.  When the thread
 *     ends, the thread unit sets the slot to NULL first and drops its reference afterwards,
 *     so the slot never names an object that may already be freed.
 *   - It also makes the one RT_K_PROCESS object and registers it, once, with
 *     rt_handle_set_process_object (rt/handle.h), which is what (HANDLE)-1 resolves to.
 * While the slot is NULL (a thread that has ended, in the last moments of its exit path),
 * and on a host thread that is not a guest thread, using (HANDLE)-2 is a fatal report that
 * says so; the same holds for (HANDLE)-1 until a process object is registered.  A slot that holds
 * anything but an RT_K_THREAD object made by rt_object_new is fatal when it is read. */

typedef struct RtThread {
    Ctx       ctx;              /* the guest registers.  ctx.host == this; ctx.gs_base == teb */
    uint64_t  miss_target;      /* written by rt_lookup just before it returns rt_dispatch_miss */
    uint32_t  id;               /* guest thread id: a multiple of 4, never reused; also at teb+0x48 */
    uint32_t  in_host;          /* 1 while the thread is inside a shim (set and cleared by the thunks,
                                   cleared around guest calls made from a shim); 0 in lifted code */
    struct RtShimFrame *shim;   /* innermost shim the thread is in, or NULL */
    uint64_t  teb;              /* guest address of the thread block (16 KB region) */
    uint64_t  stack_region;     /* base of the guest stack region, guard included (teb+0x1478) */
    uint64_t  stack_limit;      /* lowest usable guest stack address (teb+0x10) */
    uint64_t  stack_base;       /* top of the guest stack (teb+0x08) */
    uint32_t  tso;              /* 1 if the x86 memory-ordering mode is on for this host thread */
    uint32_t  is_main;          /* 1 for guest thread 1 (the host's main thread) */
    char      name[64];         /* for reports; "" if never named */
    void     *sub[RT_SUB_COUNT];
    struct RtThread *next;      /* all attached threads, newest first; guarded by the thread unit's lock */
} RtThread;

#define RT_THREAD(ctx)  ((RtThread *)(ctx)->host)

/* The calling host thread's RtThread, or NULL if it is not a guest thread.  For code that
 * has no ctx at hand.  Cheap (one host thread-local read).  Takes no lock. */
RtThread *rt_thread_self(void);

/* The Ctx of guest thread 1.  Valid after rt_init; only the main host thread may use it.
 * Fatal if called before guest thread 1 is attached (see next). */
Ctx *rt_main_ctx(void);

/* NO GUEST THREAD DURING rt_init.  Guest thread 1 is attached to the calling host thread by
 * rt_thread_attach_main, which is the LAST step of rt_init (rt/init.h): after every
 * subsystem's init function, after RtConfig.before_seal and after the dispatch table is
 * sealed.  A subsystem's rt_<name>_init, and anything it calls, therefore runs with:
 *   - no Ctx: rt_main_ctx() is fatal, and nothing that takes a `Ctx *` can be called
 *     (rt_guest_call, rt_wait, rt_cs_enter, the FAIL macro of rt/shim.h);
 *   - rt_thread_self() == NULL;
 *   - no pseudo-handle: (HANDLE)-1 and (HANDLE)-2 are fatal (see RT_SUB_HANDLE above);
 *   - no critical section: rt_cs_enter needs the caller's thread id from a Ctx.  An init
 *     function that needs a lock uses a host lock (pthread mutex, os_unfair_lock);
 *   - last error kept outside any thread block (rt_thread_set_last_error, below).
 * It may use guest memory (rt_vm_alloc, rt_galloc), the handle table, paths and the log,
 * in the order rt/init.h gives. */

/* Guest address of the process block (the PEB that every thread block points to at +0x60;
 * offsets in rt/win/teb.h).  It exists from early in rt_init, before the subsystems' init
 * functions run, so each subsystem stores its own field there (the heap unit: the process
 * heap at RT_PEB_PROCESS_HEAP). */
uint64_t rt_process_block(void);

/* Set or read the calling thread's last error (thread block +0x68), for core functions that
 * have no ctx.  On a host thread that is not a guest thread (start-up before guest thread 1
 * is attached; a host thread a test starts) the value is kept in a variable local to that
 * host thread, which starts at 0.  rt_thread_attach_main copies the main host thread's
 * value into the new thread block if it is not 0.  Neither function takes a lock or fails. */
void  rt_thread_set_last_error(DWORD e);
DWORD rt_thread_get_last_error(void);

/* Record a name for reports and give it to the host thread (pthread_setname_np).  `name`
 * is bytes in code page 1252; it is truncated to 63 bytes. */
void rt_thread_set_name(RtThread *t, const char *name);

/* ---- the list of running guest threads -------------------------------------------------- */
/* A guest thread is RUNNING from the moment it is put in the list (guest thread 1: in
 * rt_thread_attach_main; a created thread: inside rt_thread_create, before the host thread
 * is started, so also while it waits for its first ResumeThread; an attached thread: inside
 * rt_thread_attach) until its exit path takes it out, which happens after its last guest
 * code and BEFORE its thread block, stack and TLS blocks are freed.  So under the thread
 * lock every thread in the list has its guest memory, and nothing else is promised. */

/* Call fn(t, user) for every running guest thread, newest first, with the thread lock held
 * (see LOCKS for what fn may do).  (Meant for the loader's static TLS; the loader keeps its
 * own list of the threads it has recorded instead, rt/module.h, and does not call this.)
 * Cannot fail; sets no last error.
 * Fatal: fn NULL; called with the thread lock held. */
void rt_thread_for_each(void (*fn)(RtThread *t, void *user), void *user);

/* THE WAIT REPORT OF EVERY GUEST THREAD, for a run that does not end (core/thread_report.c).
 * rt_thread_report_waits writes to descriptor `fd`, between the lines "THE WAITS OF EVERY
 * GUEST THREAD (<n> running; newest first)" and "END OF THE WAITS", one block per running
 * guest thread; rt_thread_report_one appends one thread's block to buf[at..n) and returns the
 * new length (NUL-terminated, truncated when full).  A block:
 *
 *   guest thread 0x<id> "<name>"[ (guest thread 1)][; host thread <running | waiting | ...>, <ms> ms of CPU so far]
 *   [  SUSPENDED: created suspended and not resumed yet ...]
 *   the wait core's lines for the thread (rt/sync.h, THE WAIT REPORT): which objects, since
 *     when, who signalled each last and on which side of the wait's beginning
 *   inside a Windows function: the lines of a fatal report for it ("called from <module+rva>",
 *     the argument registers, "in shim <library>!<name>, called from ..." for each shim it is
 *     in, "guest stack (return addresses, innermost first):" as module+rva), further frames up
 *     to 48, and "memory at rcx: <5 x 8 bytes>" when rcx points at readable guest memory: for a
 *     wait on a lock embedded in the game's data (a critical section, a slim lock, a condition
 *     variable) rcx is that lock, and the third 8 bytes of a critical section are its owner
 *   else: "not inside a Windows function: running the game's code ..." and "guest stack,
 *     SCANNED ...": the frames found by scanning upwards from Ctx.rsp (the file's comment)
 *
 * rt_thread_report_waits is made for a signal handler: it allocates nothing, only tries the
 * thread lock and the dispatcher lock (200 ms each, then goes on without and says so), and
 * holds each other thread still (thread_suspend) while it reads it.  It changes nothing of
 * the guest's state.  A caller that must end in any case arms a limit of its own first.
 * rt_thread_report_one takes no lock and suspends nothing: for the calling thread itself,
 * for a thread the caller knows to be asleep, and for tests.  Neither sets a last error. */
void   rt_thread_report_waits(int fd);
size_t rt_thread_report_one(char *buf, size_t n, size_t at, RtThread *t);

/* Number of running guest threads now (1 after rt_init).  Takes the thread lock; the value
 * may be stale when it is returned.  For reports and tests. */
unsigned rt_thread_count(void);

/* Number of host threads that rt_thread_create has started and that have not yet run their
 * exit path to its very end (atomic; lowered as the last thing such a host thread does, after
 * step 8).  A thread object is signalled at step 7, so for a moment after a wait on it has
 * returned its host thread is still running and may hold a host lock (the log's, the handle
 * unit's).  That matters to exactly one kind of caller: code that FORKS, which copies a held
 * lock into a child that has nobody to release it.  A test that uses T_TRAPS (it forks)
 * after a thread has ended first waits for this count to come back to what it was (0 if no
 * other created thread runs).  Takes no lock. */
unsigned rt_thread_host_threads(void);

/* ---- hooks other units fill ------------------------------------------------------------- */
/* Per-thread work that belongs to another unit.  core/thread.c calls each at the place
 * stated and holds a WEAK definition that does nothing; the owning unit defines the function
 * in its own file (as rt_crash_init and its siblings, rt/init.h).  Each is called with no
 * lock held and for EVERY guest thread, guest thread 1 included (for it: at the end of
 * rt_thread_attach_main, when no lifted module is loaded yet).
 *
 *   STATIC TLS meets the thread unit in the two loader hooks, which are the loader's own
 *   functions (specified in rt/module.h, defined in core/loader_load.c; no weak definition
 *   here, so a program without the loader does not link).  Who does what: the thread unit
 *   makes the thread block zeroed, which includes the array at
 *   t->teb + RT_TEB_STATIC_TLS_ARRAY, and stores that array's address at teb+0x58; it
 *   never reads or writes the array again and allocates no TLS block.  The loader keeps
 *   its own list of threads, fills the array and frees the blocks:
 *   rt_loader_thread_attach  on the new thread's own host thread, before its start routine
 *                            and inside the frame rt_thread_exit unwinds to: records the
 *                            thread with the loader, allocates its copy of every loaded
 *                            module's TLS template and stores the addresses in the array;
 *                            then the TLS callbacks and DllMain(DLL_THREAD_ATTACH) of every
 *                            loaded module in load order.  Not called for guest thread 1
 *                            (the loader records it at its first module load).  A thread
 *                            that is created suspended and never resumed, or discarded
 *                            before it ran, was never attached and owns no TLS block.
 *   rt_loader_thread_detach  on the exiting thread, after its FLS callbacks and while its
 *                            thread block still exists: DllMain(DLL_THREAD_DETACH) in
 *                            reverse load order, the last guest code the thread runs; then
 *                            the loader frees the thread's TLS blocks and forgets the
 *                            thread.  Not called when the process exits.
 *   rt_crash_thread_enter    on the thread's own host thread before any guest code: the
 *                            alternate signal stack (section 6.1 step 4).
 *   rt_crash_thread_leave    on that host thread, the last thing the exit path does that
 *                            may use `t`.
 *   rt_file_thread_exit      on the exiting thread, after rt_wait_thread_exit: free
 *                            t->sub[RT_SUB_FILE]. */
void rt_loader_thread_attach(Ctx *ctx);
void rt_loader_thread_detach(Ctx *ctx);
void rt_crash_thread_enter(RtThread *t);
void rt_crash_thread_leave(RtThread *t);
void rt_file_thread_exit(RtThread *t);

/* ---- creating and ending a thread (core/thread_run.c) ----------------------------------- */

/* CreateThread(security, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags, lpThreadId).
 *   ctx         the calling guest thread's
 *   stack_size  requested guest stack in bytes.  The stack is the larger of this and 1 MiB,
 *               rounded up to 64 KB, committed whole, with 64 KB below it that is never
 *               accessible (section 5.3).  The host stack is four times that, at least 4 MiB
 *   start       guest address of DWORD WINAPI start(LPVOID).  0 is fatal (Windows starts a
 *               thread that faults at address 0)
 *   param       passed to start in rcx
 *   flags       0, CREATE_SUSPENDED (the thread exists and is in the list but runs nothing
 *               until rt_thread_resume), STACK_SIZE_PARAM_IS_A_RESERVATION (accepted and
 *               without effect: the rule for stack_size already covers it), or both.  Any
 *               other bit is fatal (Windows is believed to fail with
 *               ERROR_INVALID_PARAMETER; not verified)
 *   thread_id   NULL, or where the new thread's id is stored on success
 * The new thread gets: an id (a multiple of 4, above RT_FIRST_THREAD_ID, never reused); a
 * zeroed thread block with the fields of section 6.2 and last error 0; registers all zero
 * but rsp, mxcsr 0x1F80, direction flag clear; an empty name; a thread object (RT_K_THREAD)
 * that is not signalled and whose exit code reads STILL_ACTIVE.  On its own host thread it
 * then, in order: becomes rt_thread_self(); sets the CPU modes (below); calls
 * rt_crash_thread_enter; waits for rt_thread_resume if created suspended; calls
 * rt_loader_thread_attach; calls start(param) through rt_guest_call.  When start returns,
 * the thread ends as by rt_thread_exit(ctx, (DWORD)rax).  The thread may already be running,
 * or have ended, when this function returns.
 * Returns a new handle to the thread object.  Fails with NULL and last error, nothing
 * created and *thread_id not written:
 *   ERROR_NOT_ENOUGH_MEMORY   stack_size above 1 GiB (this runtime's limit); no host or guest
 *                             memory for the thread; the handle table is full; the host
 *                             refuses another thread (EAGAIN)
 * Takes the thread lock (to list the thread).  The new thread's static TLS blocks are made
 * on the new thread itself, by rt_loader_thread_attach. */
HANDLE rt_thread_create(Ctx *ctx, SIZE_T stack_size, GUESTFN start, uint64_t param, DWORD flags, DWORD *thread_id);

/* CPU MODES.  Every guest thread passes through one hook on its own host thread before it
 * runs guest code (core/thread_priv.h: rt_thread_cpu_modes): guest thread 1, every created
 * thread and every attached thread.  The hook
 *   - turns on the x86 memory-ordering mode of the host thread
 *     (thread_set_x86_64_compat(1); per thread and not inherited) according to
 *     rt_config()->tso, which the environment variable RT_TSO overrides: `require` (the app
 *     bundle): a refusal is fatal.  `try` (tests, which are plain programs the kernel
 *     refuses): a refusal is logged on the thread channel at info level and the thread runs
 *     with arm64's weak ordering.  `off`: the call is not made.  RtThread.tso says which;
 *   - sets the host float mode (tf_fp_enter_host); rt_guest_call switches to the guest's.
 * Nothing else in the runtime calls thread_set_x86_64_compat. */

/* ExitThread(dwExitCode), and the path taken when a start routine returns.  Never returns.
 * On a thread made by rt_thread_create, at any depth of guest calls and shims: the host
 * stack is unwound with longjmp to the thread's base frame (host frames in between are
 * dropped without running anything: a shim that holds a host lock across a guest call would
 * leave it held), and then, in this order:
 *   1. the thread's FLS callbacks (rt_fls_thread_exit, core/thread_priv.h)
 *   2. rt_loader_thread_detach, which also frees the thread's static TLS blocks.  No guest
 *      code runs on the thread after this
 *   3. rt_wait_thread_exit (rt/sync.h): mutexes it owns become abandoned, queued APCs are dropped
 *   4. rt_file_thread_exit
 *   5. the thread leaves the list (thread lock)
 *   6. its TLS expansion array, guest stack and thread block are freed; rt_thread_self()
 *      is NULL from here on, and RtThread.teb and ctx.gs_base are 0
 *   7. under the dispatcher lock the thread object becomes signalled with exit code `code`
 *      (rt_signal): every wait on a handle to it is satisfied from now on, and nothing is
 *      taken by such a wait
 *   8. RtThread.sub[RT_SUB_HANDLE] is cleared, rt_crash_thread_leave is called, the thread
 *      unit's reference to the object is dropped and the host thread ends.
 * So a thread that has waited for the handle sees a thread that is gone from the list, with
 * its guest memory released.  (Section 6.3 lists step 7 before step 6; this order is the one
 * built.)  The RtThread itself lives until the last reference to the thread object goes.
 * A code of 259 makes the ended thread read as STILL_ACTIVE, as on Windows.
 * On GUEST THREAD 1: if it is the only running guest thread the call is
 * rt_process_exit(ctx, code); if others are running it is fatal (on Windows the process
 * would live on without its first thread; the host's main thread cannot do that).
 * On a thread made by rt_thread_attach it is fatal: there is no frame to unwind to; such a
 * thread ends with rt_thread_detach.
 * Fatal also: ctx is not the calling thread's own; called a second time for one thread (from
 * an FLS callback or DllMain(DLL_THREAD_DETACH)); called on a created thread from inside an
 * entry point or TLS callback the loader called (rt_loader_in_entry: DllMain with
 * DLL_THREAD_ATTACH, or DLL_PROCESS_ATTACH of a module the thread is loading), because the
 * unwind would leave the loader lock held; the report names the module and the reason. */
_Noreturn void rt_thread_exit(Ctx *ctx, DWORD code);

/* ResumeThread(hThread).  Because SuspendThread is a stop (section 6.3), a suspend count is
 * 1 for a thread created with CREATE_SUSPENDED that has not been resumed and 0 otherwise.
 * Returns the count BEFORE the call: 1 (the thread now starts; exactly one call gets this
 * answer) or 0 (the thread was not suspended; nothing happens).  The pseudo-handle
 * (HANDLE)-2 names the calling thread and gives 0.
 * Fails with (DWORD)-1 and last error ERROR_INVALID_HANDLE (not an open handle, or not a
 * thread).  A thread that has ended gives 0 and nothing happens (Windows: Wine
 * kernel32/tests/thread.c, test_CreateThread_suspended).  Last error is left alone for that
 * case; what Windows sets there is not verified. */
DWORD rt_thread_resume(HANDLE thread);

/* ---- looking at and adjusting a thread (core/thread_ctl.c) ------------------------------ */

/* GetExitCodeThread(hThread, lpExitCode): *code = STILL_ACTIVE (259) while the thread has
 * not reached step 7 of rt_thread_exit, afterwards the code it ended with.  TRUE, or FALSE
 * with last error ERROR_INVALID_HANDLE (not an open handle, or not a thread) and *code not
 * written.  Accepts (HANDLE)-2.  Fatal: code NULL (Windows raises an access violation). */
BOOL rt_thread_get_exit_code(HANDLE thread, DWORD *code);

/* SetThreadPriority / GetThreadPriority.  The value is recorded in the thread object and
 * has no other effect (section 6.3); a new thread starts at THREAD_PRIORITY_NORMAL (0), and
 * an ended thread still records and reports.
 * rt_thread_set_priority: `priority` is one of the seven THREAD_PRIORITY_ values of
 * rt/win/process.h (-15, -2, -1, 0, 1, 2, 15).  TRUE, or FALSE with ERROR_INVALID_HANDLE.
 * Any other value is fatal (which of them Windows accepts in which priority class is not
 * verified).
 * rt_thread_get_priority: the recorded value, or THREAD_PRIORITY_ERROR_RETURN (0x7FFFFFFF)
 * with ERROR_INVALID_HANDLE.  Both accept (HANDLE)-2. */
BOOL rt_thread_set_priority(HANDLE thread, int priority);
int  rt_thread_get_priority(HANDLE thread);

/* SetThreadAffinityMask(hThread, dwThreadAffinityMask): record `mask` for the thread and
 * return the mask it had.  The SYSTEM MASK has one bit per reported processor: bits 0 to
 * rt_machine()->cpu_count - 1.  A thread has the process's mask until it sets a mask of its own,
 * and again after every rt_process_set_affinity, whether or not it set one before (Windows gives
 * every thread the new process mask: Wine kernel32/tests/thread.c, test_thread_processor, where
 * SetThreadGroupAffinity exists).
 * A mask whose LOW 32 BITS are all set means all processors, as Windows Vista and later read
 * it (the source is Wine kernel32/tests/thread.c): ~0 and ~0 >> 3 both succeed, return the
 * previous mask and store the PROCESS's mask.  Only the low 32 bits decide this.
 * Nothing is done to the host thread (macOS on Apple silicon has no affinity).
 * Fails with 0 and last error
 *   ERROR_INVALID_HANDLE      not an open handle, or not a thread
 *   ERROR_INVALID_PARAMETER   mask is 0, or has a bit outside the PROCESS's current mask,
 *                             unless its low 32 bits are all set (above) */
DWORD_PTR rt_thread_set_affinity(HANDLE thread, DWORD_PTR mask);

/* SetThreadIdealProcessor(hThread, dwIdealProcessor): record the processor the thread would
 * like to run on and return the one recorded before (unit rt-census-rows; the cases and their
 * sources are docs/reviews/census-rows.md 1.5).  Nothing is done to the host thread: the
 * value is a hint to a scheduler this runtime does not have (status `advisory`).
 *   processor below rt_machine()->cpu_count: stored; returns the previous value.  A thread
 *       starts with 0 (THIS RUNTIME'S CHOICE: the page gives no starting value; 0 is what
 *       every one of the recording's seven calls returned).
 *   processor 64 (MAXIMUM_PROCESSORS): returns the stored value and changes nothing (the
 *       page: "returns the current ideal processor without changing it"; Wine
 *       kernel32/tests/thread.c, test_thread_processor: succeeds).
 *   processor above 64: (DWORD)-1 with ERROR_INVALID_PARAMETER (the same test, asserted for
 *       MAXIMUM_PROCESSORS + 1 on 64-bit).
 *   not an open handle, or not a thread: (DWORD)-1 with ERROR_INVALID_HANDLE (the handle
 *       layer's answer for a wrong kind; no test read asserts it for this function).
 * Success leaves last error alone.  Accepts (HANDLE)-2.
 * STOPS: a processor from cpu_count up to 63, naming the count (Windows refuses a processor
 * the machine does not have, by Wine's implementation only; no test asserts the code.  With
 * RT_CPUS 2 or less the video decoder's request for processor 2 lands here). */
DWORD rt_thread_set_ideal_processor(HANDLE thread, DWORD processor);

/* OpenThread(access, inherit, dwThreadId): a new handle to the RUNNING guest thread with
 * this id.  Fails with NULL and last error
 *   ERROR_INVALID_PARAMETER   no running thread has this id.  DIFFERS FROM WINDOWS, which
 *                             also finds a thread that has ended while a handle to it is
 *                             still open; that case is logged once at warn level
 *   ERROR_NOT_ENOUGH_MEMORY   the handle table is full
 * Takes the thread lock to find the thread and take a reference to its object. */
HANDLE rt_thread_open(DWORD thread_id);

/* For a shim that is given a thread handle and needs the RtThread (QueueUserAPC, which then
 * calls rt_apc_queue of rt/sync.h): the thread behind `thread`, and in *object a reference
 * to its thread object that the caller drops with rt_object_unref when it is done with the
 * RtThread.  Accepts (HANDLE)-2.  An ended thread is returned too (its RtThread lives as
 * long as the object; rt_apc_queue then fails by its own rule).
 * Fails with NULL, *object NULL and last error ERROR_INVALID_HANDLE.  Fatal: object NULL. */
RtThread *rt_thread_from_handle(HANDLE thread, RtObject **object);

/* ---- host threads the runtime did not create (core/thread_attach.c) --------------------- */

/* Make the calling host thread (an audio callback, a dispatch queue worker) a guest thread:
 * it gets what rt_thread_create gives a thread (id, thread block, a 1 MiB guest stack,
 * thread object, a place in the list, the CPU modes, rt_crash_thread_enter,
 * rt_loader_thread_attach) and may then call guest code.
 *   name   NULL, or a name as for rt_thread_set_name
 * Returns the thread's RtThread; if the calling host thread already is a guest thread,
 * that thread's, unchanged (name ignored).  Fatal: no memory (there is no caller to fail to).
 * rt_thread_detach(t) ends it as steps 1 to 8 of rt_thread_exit with exit code 0 and without
 * unwinding, and returns; the host thread is then no guest thread.  It must be called on
 * the thread's own host thread before that host thread ends.  Fatal, checked in this order:
 * t is NULL or not the calling thread's own RtThread; t was not made by rt_thread_attach;
 * called from inside a shim (t->shim != NULL). */
RtThread *rt_thread_attach(const char *name);
void      rt_thread_detach(RtThread *t);

/* ---- TLS slots (core/thread_tls.c) ------------------------------------------------------ */
/* 1,088 indices: 0 to 63 are the 8-byte slots in the thread block at RT_TEB_TLS_SLOTS, 64 to
 * 1,087 are in the thread's EXPANSION ARRAY, one zeroed rt_galloc block of 8,192 bytes whose
 * address is at RT_TEB_TLS_EXPANSION; it is made at that thread's first rt_tls_set of such
 * an index and freed by the exit path (step 6).  The values live in guest memory because
 * inlined guest code reads them there.  Which indices are allocated is one process-wide
 * bitmap under the thread lock.  STATIC TLS (the array at gs:[0x58]) is the loader's and
 * has nothing to do with these functions (see rt_loader_thread_attach). */

/* TlsAlloc: the lowest index that is not allocated.  Its slot is zero in every running
 * thread (rt_tls_free leaves it so).  Needs no guest thread.  Fatal when all 1,088 are
 * allocated (Windows returns TLS_OUT_OF_INDEXES; with which last error is not verified). */
DWORD rt_tls_alloc(void);

/* TlsFree: the index becomes free, and its slot is set to zero in every running thread
 * (in the expansion array only of threads that have one), all under the thread lock.
 * TRUE, or FALSE with last error ERROR_INVALID_PARAMETER (index >= 1,088, or not allocated). */
BOOL rt_tls_free(DWORD index);

/* TlsGetValue: the calling thread's value for `index`, with last error set to ERROR_SUCCESS
 * (the exception stated at the top: a stored 0 and a failure are told apart by it).  An
 * index of the expansion range reads 0 while the thread has no expansion array; none is
 * made.  Whether the index is allocated is NOT checked, as on Windows.
 * Fails with 0 and last error ERROR_INVALID_PARAMETER: index >= 1,088.  Takes no lock. */
uint64_t rt_tls_get(Ctx *ctx, DWORD index);

/* TlsSetValue: store `value` in the calling thread's slot; makes the thread's expansion
 * array when needed.  Whether the index is allocated is NOT checked, as on Windows.
 * TRUE, or FALSE with last error ERROR_INVALID_PARAMETER: index >= 1,088.  Takes no lock. */
BOOL rt_tls_set(Ctx *ctx, DWORD index, uint64_t value);

/* Guest address of the calling thread's slot `index` (the 8 bytes rt_tls_get reads).  With
 * `create`, the expansion array is made if the index needs it and the thread has none.
 * Returns 0 if index >= 1,088, or if the index is in the expansion range, the thread has no
 * expansion array and `create` is false.  Sets no last error.  Takes no lock. */
uint64_t rt_tls_slot(Ctx *ctx, DWORD index, bool create);

/* ---- FLS slots (core/thread_fls.c) ------------------------------------------------------ */
/* Fibers are not supported (section 6.3), so fiber-local storage is a second set of
 * per-thread slots, each index with an optional callback that runs when a thread ends with a
 * nonzero value and when the index is freed.  RT_FLS_SLOTS indices; index 0 is never handed
 * out and is invalid (so on Windows: Wine fiber.c test_FiberLocalStorage).  The values are host-side: an array
 * of RT_FLS_SLOTS 8-byte values per thread, made at the thread's first rt_fls_set. */
#define RT_FLS_SLOTS 128

/* FlsAlloc(lpCallback): the lowest index from 1 that is not allocated, with `callback`
 * (VOID WINAPI fn(PVOID value), or 0 for none) recorded for it.  Fatal when all 127 are
 * allocated (Windows returns FLS_OUT_OF_INDEXES; the last error is not verified). */
DWORD rt_fls_alloc(GUESTFN callback);

/* FlsFree(dwFlsIndex): under the thread lock, collect every running thread's nonzero value
 * for the index, setting each to zero, and free the index; then, with NO lock held and on
 * the CALLING thread, call callback(value) through rt_guest_call once per collected value,
 * the calling thread's own first and then the others newest thread first.  (Windows runs
 * them before FlsFree returns, on the calling thread, also for a value of a fibre that is
 * not running: Wine fiber.c test_FiberLocalStorageWithFibers; other threads' values are
 * not tested there, so that part is not verified.)  No callback: the values are only zeroed.
 * TRUE, or FALSE with last error ERROR_INVALID_PARAMETER (index 0, >= RT_FLS_SLOTS, or not
 * allocated).  Fatal: the calling host thread is not a guest thread and a callback is due. */
BOOL rt_fls_free(Ctx *ctx, DWORD index);

/* FlsGetValue: the calling thread's value, 0 if it never set one.  Last error is set to
 * ERROR_SUCCESS when it succeeds, as rt_tls_get does (Wine fiber.c test_FiberLocalStorage:
 * last error is preset to 0xdeadbeef, FlsGetValue returns the value stored just before, and
 * last error must then be ERROR_SUCCESS; no alternative is allowed).  An index in range that
 * is not allocated reads 0 without error.  Fails with 0 and last error ERROR_INVALID_PARAMETER:
 * index 0 or >= RT_FLS_SLOTS.  No lock.
 * The case the body's author must add to its test (no test is written here): set last error
 * to 0xdeadbeef; rt_fls_set(ctx, i, 0x217); rt_fls_get(ctx, i) returns 0x217 and last error is
 * ERROR_SUCCESS (0).  The value is nonzero; a stored 0 is not tested separately. */
uint64_t rt_fls_get(Ctx *ctx, DWORD index);

/* FlsSetValue: store `value` for the calling thread.  TRUE, or FALSE with last error
 * ERROR_INVALID_PARAMETER (index 0 or >= RT_FLS_SLOTS).  An index in range that is not
 * allocated is not a stop: the value is stored and TRUE is returned, and a later FlsGetValue
 * on that index returns it.  Source (Windows, the same test as in
 * docs/reviews/win-behaviour-check-2.md, W3): FlsFree(127) fails as not allocated, then
 * FlsSetValue(127, 0x217) succeeds and FlsGetValue(127) returns 0x217.
 * Test case, to be written with the body (not yet in runtime/tests): for an index from 1 to
 * 127 never allocated, FlsSetValue returns TRUE and FlsGetValue on that index returns the value.
 * Not verified: what a later FlsAlloc that hands out this index finds (whether it sees the
 * stored value).
 * Takes no lock. */
BOOL rt_fls_set(Ctx *ctx, DWORD index, uint64_t value);

/* ---- the COM state of a thread (core/thread_com.c) --------------------------------------- */
/* There is no COM run time (section 3.7): no class can be created and no apartment exists.
 * What is kept is the bookkeeping that CoInitializeEx, CoUninitialize and OleInitialize are
 * documented to do, so that a caller's pairs of calls get the results Windows gives them.
 * Per thread: a count of initialisations, the concurrency model of the first of them, and a
 * count of OLE initialisations.  A thread reads and writes only its own, so no lock is
 * taken; a thread that ends with a count above 0 ends like any other.  None of the three
 * functions touches last error (they return an HRESULT or nothing).  Fatal in all three:
 * ctx is not a guest thread's.
 * NOT MODELLED, because nothing here can observe it: on Windows a thread that has not
 * initialised COM belongs to the multithreaded apartment as soon as another thread has
 * created it (from memory).  That changes what CoCreateInstance and its relatives answer,
 * which is decided elsewhere (section 3.7), and not what these three return.
 * Sources: Microsoft's pages of CoInitializeEx (combaseapi.h), CoInitialize and
 * OleInitialize (ole2.h), read 2026-10-10; Wine's ole32/tests/compobj.c where named. */

/* CoInitializeEx(pvReserved, dwCoInit) for a pvReserved of NULL, which is the shim's to
 * check; CoInitialize(NULL) is this with COINIT_APARTMENTTHREADED.
 *   coinit   the concurrency model, COINIT_MULTITHREADED (0) or COINIT_APARTMENTTHREADED
 *            (0x2), alone or with COINIT_DISABLE_OLE1DDE (0x4) and COINIT_SPEED_OVER_MEMORY
 *            (0x8), which are accepted and change nothing (the page: "Any combination of
 *            values from COINIT can be used")
 * Returns, in the words of the page:
 *   S_OK                the thread's count was 0: it becomes 1 and the model is recorded
 *                       ("The COM library was initialized successfully on this thread")
 *   S_FALSE             the count was above 0 and the model is the recorded one: the count
 *                       goes up by one ("The COM library is already initialized on this
 *                       thread"; "subsequent valid calls return S_FALSE")
 *   RPC_E_CHANGED_MODE  the count was above 0 and the model is the other one: nothing
 *                       changes ("If the concurrency flag does not match, then the call
 *                       fails and returns RPC_E_CHANGED_MODE")
 * Every S_OK and S_FALSE is to be balanced by one rt_com_uninitialize ("each successful call
 * to CoInitialize or CoInitializeEx, including any call that returns S_FALSE, must be
 * balanced by a corresponding call to CoUninitialize"); an RPC_E_CHANGED_MODE is not.
 * Fatal: a bit of coinit outside the four COINIT_ values (Windows is believed to return
 * E_INVALIDARG; not verified); a count that would pass 0xFFFFFFFF. */
HRESULT rt_com_initialize(Ctx *ctx, DWORD coinit);

/* CoUninitialize(): drop one initialisation of the calling thread.  At a count of 0 the
 * thread is uninitialised again: its recorded model is forgotten and the next
 * rt_com_initialize may name either.  The OLE count stays as it is (Wine compobj.c,
 * test_OleInitialize_InitCounting: "CoUninitialize does not formally deinit Ole").
 * A call on a thread whose count is 0 changes nothing and is logged at warn level on the
 * `thread` channel: the function returns nothing, so Windows cannot report it either. */
void rt_com_uninitialize(Ctx *ctx);

/* OleInitialize(pvReserved) for a pvReserved of NULL (the shim's to check).  The page:
 * "OleInitialize calls CoInitializeEx internally to initialize the COM library on the
 * current apartment", as a single-threaded apartment.  So:
 *   1. rt_com_initialize(ctx, COINIT_APARTMENTTHREADED).  If that is RPC_E_CHANGED_MODE it
 *      is the result and nothing changes (the page: "A call to OleInitialize on an
 *      apartment that was previously initialized as multithreaded will fail").
 *   2. Otherwise the thread's OLE count goes up by one, and the result is S_OK if it was 0
 *      and S_FALSE if not, whatever step 1 returned.
 * Step 2 is what Windows does and differs from "the result of CoInitializeEx": Wine
 * compobj.c test_CoInitializeEx gets S_OK from the first OleInitialize on a thread that
 * CoInitializeEx had initialised already and S_FALSE from the second, and
 * test_OleInitialize_InitCounting gets S_FALSE from an OleInitialize that follows
 * CoInitialize, OleInitialize and two CoUninitialize.
 * A successful call holds one initialisation of step 1, which on Windows OleUninitialize
 * gives back.  No boot-path DLL imports OleUninitialize, so nothing lowers the OLE count
 * and no function for it exists.  Nothing of OLE itself is behind this (no clipboard, no
 * drag and drop): the functions that would need it are stops. */
HRESULT rt_ole_initialize(Ctx *ctx);

/* ---- the process (core/process.c) ------------------------------------------------------- */
/* There is one process.  Its object (RT_K_PROCESS, what (HANDLE)-1 names) is made and
 * registered by rt_thread_attach_main; it is never signalled, because the host process ends
 * when the guest process does.  DLL_PROCESS_DETACH is not delivered before Gate 5 (section
 * 1.6), so no guest code runs when the process exits: no FLS callback, no DllMain. */

/* ExitProcess, TerminateProcess and GetExitCodeProcess are NOT here: rt_process_exit,
 * rt_process_terminate and rt_process_exit_code are specified in rt/crash.h and live in
 * core/exit.c, with the rest of what ends the process (planner's decision, 2026-10-09: two
 * contract units had specified them; the exit sequence must know about a fatal report in
 * progress, which is the crash unit's).  rt_thread_exit on the last thread calls
 * rt_process_exit. */

/* SetPriorityClass / GetPriorityClass: recorded in the process object, no other effect.
 * Starts as NORMAL_PRIORITY_CLASS.  `cls` is one of the six *_PRIORITY_CLASS values of
 * rt/win/process.h.  Set: TRUE, or FALSE with ERROR_INVALID_HANDLE, or with
 * ERROR_INVALID_PARAMETER for any other value except the two PROCESS_MODE_BACKGROUND_
 * values, which are fatal (not handled).  Get: the class, or 0 with ERROR_INVALID_HANDLE. */
BOOL  rt_process_set_priority_class(HANDLE process, DWORD cls);
DWORD rt_process_get_priority_class(HANDLE process);

/* GetProcessAffinityMask / SetProcessAffinityMask.  The process's mask starts as the system
 * mask (see rt_thread_set_affinity).  Get stores both; fatal if either pointer is NULL.
 * Set records `mask`.  TRUE, or FALSE with last error
 *   ERROR_INVALID_HANDLE      not (HANDLE)-1 or an open handle to the process object
 *   ERROR_INVALID_PARAMETER   set: mask is 0 or has a bit outside the system mask */
BOOL rt_process_get_affinity(HANDLE process, DWORD_PTR *process_mask, DWORD_PTR *system_mask);
BOOL rt_process_set_affinity(HANDLE process, DWORD_PTR mask);

/* SetErrorMode(uMode): store `mode` as the process's error mode and return the mode it had.
 * One value for the whole process, 0 at the start.  It has no other effect: no error box
 * exists to switch off (section 9.2 names the call `advisory`); it is kept because callers
 * put back the value they were given.
 *   mode   any combination of SEM_FAILCRITICALERRORS (0x0001), SEM_NOGPFAULTERRORBOX (0x0002)
 *          and SEM_NOOPENFILEERRORBOX (0x8000), or 0
 * Cannot fail and never touches last error (the page: "The return value is the previous
 * state of the error-mode bit flags"; it gives no failure).  The swap is one atomic
 * exchange: of two threads that call at once, one is returned what the other stored.
 * Needs no guest thread and takes no lock.
 * Fatal: SEM_NOALIGNMENTFAULTEXCEPT (0x0004), of which the page says "After this value is
 * set for a process, subsequent attempts to clear the value are ignored" and that it is
 * "only supported by certain processor architectures": what an x64 system then records and
 * returns is not verified.  Any bit outside the four SEM_ values: the page lists no other
 * and does not say what Windows does with one.
 * The 0 at the start is our choice: the page says "A child process inherits the error mode
 * of its parent process", and no process created this one (as rt_env_startup_info, rt/env.h).
 * Source: Microsoft's page of SetErrorMode (errhandlingapi.h), read 2026-10-10. */
UINT rt_process_swap_error_mode(UINT mode);

/* GetProcessTimes(hProcess, lpCreationTime, lpExitTime, lpKernelTime, lpUserTime) without
 * the exit time, which the page calls "undefined" for a process that has not exited and
 * which is therefore the shim's to store.
 *   process    (HANDLE)-1 or an open handle to the process object
 *   creation   receives the moment rt_init attached guest thread 1 (the process object is
 *              made then), as a FILETIME: 100 ns units since 1601-01-01 UTC, read once from
 *              rt_time_utc_100ns (rt/time.h).  The same value on every call
 *   kernel     receives the processor time the HOST process has spent in the kernel, all
 *              its threads together, in 100 ns units (the page: it "receives the amount of
 *              time that the process has executed in kernel mode", summed over the threads
 *              of the process; one second is 10,000,000)
 *   user       the same for user mode.  Both come from one getrusage(RUSAGE_SELF) call, so
 *              they include what the runtime itself and its start-up used, never decrease,
 *              and can together exceed the wall-clock time (several processors).  The host
 *              reports them in microseconds
 * TRUE, or FALSE with last error ERROR_INVALID_HANDLE (not (HANDLE)-1 or an open handle to
 * the process object) and nothing written.  Last error is left alone on success.
 * Fatal: creation, kernel or user NULL (the page does not say that one may be NULL; the
 * shim passes its own pointers and stops on a NULL one first); getrusage failing.
 * Source: Microsoft's page of GetProcessTimes (processthreadsapi.h), read 2026-10-10. */
BOOL rt_process_times(HANDLE process, FILETIME *creation, FILETIME *kernel, FILETIME *user);

/* SetThreadExecutionState(esFlags): record the execution state the calling thread asks for
 * and return the one it had.  The host is told nothing (the display and the system are the
 * user's to keep awake): `advisory` in the sense of section 9.2.
 *   ctx     the calling guest thread's
 *   flags   any combination of ES_SYSTEM_REQUIRED (0x1), ES_DISPLAY_REQUIRED (0x2),
 *           ES_AWAYMODE_REQUIRED (0x40) and ES_CONTINUOUS (0x80000000), or 0
 * THE STATE is per thread.  A call WITH ES_CONTINUOUS replaces it by `flags` (the page:
 * the flag "Informs the system that the state being set should remain in effect until the
 * next call that uses ES_CONTINUOUS").  A call WITHOUT ES_CONTINUOUS leaves it as it is (the
 * page: "Calling SetThreadExecutionState without ES_CONTINUOUS simply resets the idle
 * timer"; this machine has no idle timer).  A new thread does not take over its creator's.
 * RETURNS the state before the call (the page: "the previous thread execution state").  A
 * thread that has never set one has ES_CONTINUOUS (0x80000000) alone: this FIRST VALUE IS
 * FROM MEMORY.  It was looked for on 2026-10-10 and is in neither source: the page does not
 * give it, and Wine's conformance tests do not call the function (kernel32/tests/power.c
 * and ntdll/tests/info.c).  The page's return values leave no room for 0 there: 0 is the
 * failure value.  Since only a call with ES_CONTINUOUS stores, every value returned has
 * that bit.
 * FAILS with 0 and last error ERROR_INVALID_PARAMETER, nothing recorded:
 *   ES_USER_PRESENT (0x4) together with any other flag (the page: "If ES_USER_PRESENT is
 *   combined with other esFlags values, the call will fail and none of the specified states
 *   will be set."  The page gives only the return value "NULL"; the error code is from memory)
 * Last error is left alone on success.
 * Fatal, because the page does not say what Windows does: ES_USER_PRESENT alone ("This
 * value is not supported"); ES_AWAYMODE_REQUIRED without ES_CONTINUOUS ("This value must be
 * specified with ES_CONTINUOUS"); any bit outside the five ES_ values.
 * Fatal also: ctx is not a guest thread's.  Takes no lock: a thread reads and writes only
 * its own state.
 * Source: Microsoft's page of SetThreadExecutionState (winbase.h), read 2026-10-10. */
EXECUTION_STATE rt_thread_execution_state(Ctx *ctx, EXECUTION_STATE flags);

#endif /* RT_THREAD_H */
