/* Kernel synchronisation objects and the wait calls (docs/RUNTIME.md section 4.2).
 *
 * Events, mutexes and semaphores are objects in the handle table (rt/handle.h) of the kinds
 * RT_K_EVENT, RT_K_MUTEX and RT_K_SEMAPHORE; core/sync.c implements them on the wait core of
 * core/wait.c.  They are not the engine's hot locks: critical sections, SRW locks, condition
 * variables and SLists live inside guest structures and never come here (rt/lock.h).
 *
 * WHICH FUNCTION A SHIM CALLS.  Each shim of this family is one call; the function does the
 * handle lookup, the work and the last error.  An `A` shim converts its name to UTF-16 and
 * calls what its `W` sibling calls.
 *
 *     CreateEventA/W          rt_event_create          CreateMutexA/W        rt_mutex_create
 *     OpenEventA/W            rt_event_open            OpenMutexW            rt_mutex_open
 *     SetEvent                rt_event_set             ReleaseMutex          rt_mutex_release
 *     ResetEvent              rt_event_reset           CreateSemaphoreA/W    rt_semaphore_create
 *     PulseEvent              rt_event_pulse           (OpenSemaphoreW)      rt_semaphore_open
 *     WaitForSingleObject[Ex]     rt_wait_handle       ReleaseSemaphore      rt_semaphore_release
 *     WaitForMultipleObjects[Ex]  rt_wait_handles      Sleep, SleepEx        rt_sleep_ex
 *     SignalObjectAndWait         rt_signal_and_wait   QueueUserAPC          rt_apc_queue
 *
 * Parameters the functions do not take are the shim's to ignore: security attributes, the
 * desired access and the inherit flag (there is one process and no access checking).
 *
 * `ctx` is always the calling guest thread's own Ctx.  It identifies the thread (a mutex is
 * owned by a thread) and receives the last error.
 *
 * LAST ERROR (section 3.4).  Set on failure and left alone on success, with the one documented
 * exception: the three Create functions always set it, to ERROR_ALREADY_EXISTS when the name
 * existed and to ERROR_SUCCESS when a new object was made (see rt_event_create).
 *
 * THREADS.  Every function here may be called from any guest thread at any time.  All state
 * is guarded by the dispatcher lock (rt/handle.h), which these functions take themselves:
 * call them WITHOUT the lock unless a comment says otherwise.
 *
 * NAMES go through the handle unit's name space, which all named kinds share: a name is
 * compared case-sensitively, "Local\X" is "X", and the handle unit stops the run on names it
 * does not handle (empty, a backslash after the prefix, longer than MAX_PATH; core/handle.c).
 *
 * THE OTHER WAITABLE KINDS.  A unit that owns a waitable kind (threads and the process,
 * signalled on exit; files, if they become waitable) needs nothing from this header: it gives
 * its RtObjectOps the is_signaled and consume operations of rt/handle.h and calls rt_signal
 * under the dispatcher lock when an object becomes signalled.  Its objects then work with
 * every wait call below.  The thread unit additionally calls rt_wait_thread_exit when a
 * thread exits.
 */
#ifndef RT_SYNC_H
#define RT_SYNC_H

#include "runtime.h"
#include "rt/wintypes.h"
#include "rt/handle.h"
#include "rt/win/base.h"
#include "rt/win/error.h"

struct RtThread;

/* ---- events --------------------------------------------------------------------------------- */
/* An event is set (signalled) or reset.  A wait on a MANUAL-RESET event takes nothing: the
 * event stays set and satisfies every wait until ResetEvent.  A wait on an AUTO-RESET event
 * resets it, so each time it is set it satisfies exactly one wait. */

/* CreateEventW(security, bManualReset, bInitialState, lpName).
 *   manual_reset   nonzero: manual-reset; zero: auto-reset
 *   initial_state  nonzero: created set
 *   name           NULL for an unnamed event, or its name
 * Returns a new handle.  If an event with this name exists, the handle is to that event, the
 * two flags are ignored and last error is ERROR_ALREADY_EXISTS; otherwise a new event is made
 * and last error is ERROR_SUCCESS (0).  (The documentation states the first half.  Setting 0
 * on a fresh creation is what kernel32 does, and callers rely on it when they test
 * GetLastError() == ERROR_ALREADY_EXISTS after a successful call.)
 * Fails with NULL and last error
 *   ERROR_INVALID_HANDLE      the name belongs to an object of another kind
 *   ERROR_NOT_ENOUGH_MEMORY   no memory, or the handle table is full */
HANDLE rt_event_create(Ctx *ctx, BOOL manual_reset, BOOL initial_state, const WCHAR *name);

/* OpenEventW(access, inherit, lpName): a new handle to the existing event `name`.
 * Fails with NULL and last error
 *   ERROR_FILE_NOT_FOUND      no object has this name
 *   ERROR_INVALID_HANDLE      the name belongs to an object of another kind
 *   ERROR_INVALID_PARAMETER   name is NULL (the handle unit's reading; core/handle.c)
 *   ERROR_NOT_ENOUGH_MEMORY   the handle table is full
 * Last error is left alone on success. */
HANDLE rt_event_open(Ctx *ctx, const WCHAR *name);

/* SetEvent.  Manual-reset: the event becomes set and every thread waiting on it whose whole
 * wait can now be satisfied is released; it stays set.  Auto-reset: if threads are waiting,
 * the first in queue order whose whole wait can be satisfied is released and the event is
 * left reset (exactly one thread per call); if none can be released it stays set until one
 * wait consumes it.  Setting an event that is already set changes nothing.
 * TRUE, or FALSE with last error ERROR_INVALID_HANDLE (not an open handle, or not an event). */
BOOL rt_event_set(Ctx *ctx, HANDLE event);

/* ResetEvent: the event becomes reset.  Nobody is woken.  Returns as rt_event_set. */
BOOL rt_event_reset(Ctx *ctx, HANDLE event);

/* PulseEvent, by the documented rule, as ONE step under the dispatcher lock: set the event,
 * release the waiting threads a SetEvent would release now (manual-reset: all that can be
 * released immediately; auto-reset: one), and leave the event RESET whether or not anyone
 * was released.  A thread that starts waiting after the call is not released by it.  A thread
 * that is busy running an APC at that moment is not waiting and misses the pulse, as on
 * Windows.  Returns as rt_event_set. */
BOOL rt_event_pulse(Ctx *ctx, HANDLE event);

/* For core units that hold a reference to an event object and have no handle or no ctx (a
 * file operation completing on a helper thread signals OVERLAPPED.hEvent): SetEvent and
 * ResetEvent on the object.  `event` must be of kind RT_K_EVENT (anything else is fatal).
 * Cannot fail; last error is not touched. */
void rt_event_set_object(RtObject *event);
void rt_event_reset_object(RtObject *event);

/* ---- mutexes -------------------------------------------------------------------------------- */
/* A mutex is owned by at most one thread.  A wait on it is satisfied when it is unowned (the
 * waiter becomes the owner) or owned by the waiter itself (the recursion depth goes up by
 * one); the owner calls ReleaseMutex once per successful wait.  A mutex whose owner exits
 * without releasing it is ABANDONED: it becomes unowned, and the next wait that acquires it
 * returns WAIT_ABANDONED_0 + index instead of WAIT_OBJECT_0 + index (see rt_wait_handles). */

/* CreateMutexW(security, bInitialOwner, lpName).
 *   initial_owner  nonzero: if THIS call creates the mutex, the calling thread owns it with
 *                  depth 1.  Ignored when the name already exists: the caller then does not
 *                  own it and must wait for it.
 * Returns, last error and failures exactly as rt_event_create. */
HANDLE rt_mutex_create(Ctx *ctx, BOOL initial_owner, const WCHAR *name);

/* OpenMutexW(access, inherit, lpName).  As rt_event_open. */
HANDLE rt_mutex_open(Ctx *ctx, const WCHAR *name);

/* ReleaseMutex: lower the calling thread's recursion depth by one; at zero the mutex becomes
 * unowned and the first waiting thread whose whole wait can be satisfied becomes the owner.
 * TRUE, or FALSE with last error
 *   ERROR_NOT_OWNER           the calling thread does not own the mutex (it is unowned, or
 *                             owned by another thread); nothing changes
 *   ERROR_INVALID_HANDLE      not an open handle, or not a mutex */
BOOL rt_mutex_release(Ctx *ctx, HANDLE mutex);

/* ---- semaphores ----------------------------------------------------------------------------- */
/* A semaphore holds a count between 0 and its maximum.  It is signalled while the count is
 * above 0; each satisfied wait lowers the count by one. */

/* CreateSemaphoreW(security, lInitialCount, lMaximumCount, lpName).
 * Returns and last error as rt_event_create; the counts are ignored when the name exists.
 * Fails with NULL and last error
 *   ERROR_INVALID_PARAMETER   maximum_count <= 0, initial_count < 0 or initial_count >
 *                             maximum_count.  Checked first, even if the name exists
 *   ERROR_INVALID_HANDLE, ERROR_NOT_ENOUGH_MEMORY   as rt_event_create */
HANDLE rt_semaphore_create(Ctx *ctx, LONG initial_count, LONG maximum_count, const WCHAR *name);

/* OpenSemaphoreW(access, inherit, lpName).  As rt_event_open. */
HANDLE rt_semaphore_open(Ctx *ctx, const WCHAR *name);

/* ReleaseSemaphore(hSemaphore, lReleaseCount, lpPreviousCount): raise the count by
 * release_count and release waiting threads in queue order, one per unit of count, while any
 * can be released.
 *   previous_count  NULL, or where the count before the call is stored (on success only)
 * TRUE, or FALSE with last error, the count unchanged and *previous_count not written:
 *   ERROR_INVALID_PARAMETER   release_count <= 0
 *   ERROR_TOO_MANY_POSTS      the count would pass the maximum
 *   ERROR_INVALID_HANDLE      not an open handle, or not a semaphore */
BOOL rt_semaphore_release(Ctx *ctx, HANDLE semaphore, LONG release_count, LONG *previous_count);

/* ---- waiting -------------------------------------------------------------------------------- */

/* WaitForMultipleObjects[Ex](nCount, lpHandles, bWaitAll, dwMilliseconds[, bAlertable]).
 *   count       1 to MAXIMUM_WAIT_OBJECTS
 *   handles     `count` handles to waitable objects (events, mutexes, semaphores, threads, the
 *               process).  The pseudo-handles are accepted, as on Windows
 *   wait_all    zero: return when ANY one object is signalled, taking that one only.
 *               Nonzero: return when ALL are signalled at once, taking all of them in one
 *               step; until then nothing is taken, so a thread never holds one of its
 *               objects while it waits for another
 *   timeout_ms  INFINITE, or the longest time to wait in milliseconds.  0 never sleeps
 *   alertable   nonzero: the wait also ends when an APC is queued to the thread (rt_apc_queue)
 * Returns
 *   WAIT_OBJECT_0 + i      wait-any: object i satisfied the wait; if several were signalled,
 *                          the lowest index.  Wait-all: WAIT_OBJECT_0
 *   WAIT_ABANDONED_0 + i   the same, and the object taken is a mutex whose owner exited
 *                          without releasing it.  The caller now owns the mutex like after
 *                          any wait and must release it; what the mutex protected may be
 *                          inconsistent.  Wait-all: WAIT_ABANDONED_0 if any of the mutexes
 *                          taken was abandoned
 *   WAIT_TIMEOUT           the time ran out; nothing was taken
 *   WAIT_IO_COMPLETION     alertable only: the thread's queued APCs were run; nothing was
 *                          taken.  An object that is signalled wins over a queued APC
 *   WAIT_FAILED            with last error
 *       ERROR_INVALID_PARAMETER   count is 0 or above MAXIMUM_WAIT_OBJECTS; or wait_all and
 *                                 the same object appears twice (through the same handle or
 *                                 two handles to one object)
 *       ERROR_INVALID_HANDLE      a handle is not open, or its object cannot be waited on
 * Nothing is taken from any object when the call fails.  `handles` NULL is fatal (Windows
 * raises an access violation). */
DWORD rt_wait_handles(Ctx *ctx, DWORD count, const HANDLE *handles, BOOL wait_all, DWORD timeout_ms, BOOL alertable);

/* WaitForSingleObject[Ex](hHandle, dwMilliseconds[, bAlertable]): rt_wait_handles with one
 * handle.  Returns WAIT_OBJECT_0, WAIT_ABANDONED_0, WAIT_TIMEOUT, WAIT_IO_COMPLETION, or
 * WAIT_FAILED with ERROR_INVALID_HANDLE. */
DWORD rt_wait_handle(Ctx *ctx, HANDLE handle, DWORD timeout_ms, BOOL alertable);

/* SignalObjectAndWait(hObjectToSignal, hObjectToWaitOn, dwMilliseconds, bAlertable): signal
 * one object and wait on another as ONE step under the dispatcher lock, so that nothing can
 * happen between the two (a pulse sent by a thread that the signal released cannot be missed).
 *   to_signal   an event (as SetEvent), a mutex (as ReleaseMutex) or a semaphore (as
 *               ReleaseSemaphore with a count of 1)
 *   to_wait     any waitable object
 * Returns as rt_wait_handle.  WAIT_FAILED with last error, and then nothing was signalled:
 *   ERROR_INVALID_HANDLE   either handle is not open; to_signal is not an event, mutex or
 *                          semaphore; to_wait cannot be waited on
 *   ERROR_NOT_OWNER        to_signal is a mutex the calling thread does not own
 *   ERROR_TOO_MANY_POSTS   to_signal is a semaphore already at its maximum */
DWORD rt_signal_and_wait(Ctx *ctx, HANDLE to_signal, HANDLE to_wait, DWORD timeout_ms, BOOL alertable);

/* SleepEx(dwMilliseconds, bAlertable); Sleep(ms) is rt_sleep_ex(ctx, ms, FALSE) with the
 * result dropped.  Sleeps `timeout_ms` milliseconds (no rounding to a 15 ms tick), or for
 * ever if INFINITE.  0 gives up the processor once and returns.
 * Returns 0 when the time has passed, or WAIT_IO_COMPLETION if `alertable` and the thread's
 * queued APCs were run instead (before sleeping if any were already queued, even with 0). */
DWORD rt_sleep_ex(Ctx *ctx, DWORD timeout_ms, BOOL alertable);

/* ---- asynchronous procedure calls ----------------------------------------------------------- */

/* QueueUserAPC(pfnAPC, hThread, dwData): queue a call of the guest function `fn(param)`
 * (VOID CALLBACK fn(ULONG_PTR)) to the thread `target`.  The thread unit turns the thread
 * handle into the RtThread.
 *
 * The call is made BY the target thread, the next time it is in an alertable wait
 * (rt_wait_handle[s], rt_signal_and_wait, rt_sleep_ex or rt_wait with `alertable`): at once
 * if it is sleeping in one now.  That wait runs every queued call in the order queued,
 * including calls queued while it runs them, through rt_guest_call with no lock held, and
 * then returns WAIT_IO_COMPLETION.  A wait that is not alertable never runs one; a thread
 * that never waits alertably never runs them, and they are dropped when it exits.
 *   ctx     the CALLING thread's Ctx, for last error; may be NULL on a host thread that has
 *           none (last error then goes through rt_thread_set_last_error)
 * TRUE, or FALSE with last error
 *   ERROR_GEN_FAILURE         the target thread has exited (rt_wait_thread_exit was called)
 *   ERROR_NOT_ENOUGH_MEMORY   no memory */
BOOL rt_apc_queue(Ctx *ctx, struct RtThread *target, GUESTFN fn, ULONG_PTR param);

/* The same queue, for a guest function of 1 to RT_APC_MAX_ARGS integer arguments: the call
 * made is fn(args[0], ..., args[nargs - 1]), each argument 64 bits wide, copied here.  It is
 * for the runtime's own completion calls, which no Windows function queues by itself: the
 * completion routine of ReadFileEx (rt/file.h, rt_file_read_ex) is
 * VOID CALLBACK fn(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped).
 * Everything else is rt_apc_queue's: one queue per thread, so calls of both forms run in the
 * order they were queued; the same results and last errors.  rt_apc_queue(ctx, t, fn, p) is
 * rt_apc_queue_args(ctx, t, fn, 1, &p).  nargs 0 or above RT_APC_MAX_ARGS, or args NULL, is
 * fatal. */
#define RT_APC_MAX_ARGS 3
BOOL rt_apc_queue_args(Ctx *ctx, struct RtThread *target, GUESTFN fn, unsigned nargs, const uint64_t *args);

/* ---- for the thread unit -------------------------------------------------------------------- */

/* A thread is exiting (section 6.3: "release held mutexes as abandoned").  Call it once per
 * thread, on that thread, after its last guest code has run and before the thread object is
 * signalled.  It
 *   - abandons every mutex the thread owns: each becomes unowned and marked abandoned, and
 *     the first thread waiting for it acquires it with WAIT_ABANDONED_0 + index;
 *   - drops the APCs still queued to the thread, which are never run;
 *   - frees the wait core's per-thread state, RtThread.sub[RT_SUB_WAIT].
 * Afterwards rt_apc_queue to the thread fails, and a wait by it is fatal.  Calling it twice
 * for one RtThread is fatal, and so is calling it for a thread that is inside a wait (only
 * TerminateThread could do that, and it is not supported: section 6.3).
 * A thread that never waited and owns nothing may be passed too. */
void rt_wait_thread_exit(struct RtThread *t);

/* ---- for units that define a waitable kind -------------------------------------------------- */
/* The dispatcher lock, rt_wait and rt_signal are declared in rt/handle.h.  What follows is
 * what this unit adds to them. */

/* True if the calling thread holds the dispatcher lock.  For assertions, and for a destroy
 * operation that needs the lock and may be reached with or without it (rt_object_unref
 * called under the lock). */
bool rt_dispatcher_held(void);

/* As rt_wait, for a caller that must change an object's state and start waiting as one step
 * (SignalObjectAndWait): the caller TAKES the dispatcher lock, makes its change, calls
 * rt_signal, and calls this WITH the lock held.  The function releases the lock (before it
 * sleeps or runs an APC, and in every case before it returns).
 * `n` may be 0 here, with objs NULL: nothing to wait for but the timeout or an APC
 * (rt_sleep_ex); the result is then WAIT_TIMEOUT or WAIT_IO_COMPLETION.  The arguments are
 * NOT checked: every object must be waitable, n at most MAXIMUM_WAIT_OBJECTS, and a wait-all
 * must not name an object twice. */
DWORD rt_wait_locked(Ctx *ctx, unsigned n, RtObject *const *objs, bool wait_all, DWORD timeout_ms, bool alertable);

/* Called by a kind's `consume` operation, and only from there: the object just taken was
 * abandoned by its previous owner.  The wait being satisfied then returns WAIT_ABANDONED_0 +
 * index instead of WAIT_OBJECT_0 + index.  (The consume operation of rt/handle.h returns
 * nothing; this is how the mutex tells the wait core.) */
void rt_wait_report_abandoned(void);

/* Number of wait entries queued on `obj` now: one per thread sleeping in a wait that names
 * it (two for a wait-any that names it twice).  Takes the dispatcher lock itself, and may
 * also be called with the lock already held.  For tests and reports; without the lock the
 * value can be stale as soon as it is returned. */
unsigned rt_wait_queue_length(RtObject *obj);

/* ---- THE WAIT REPORT: what a thread waits on, for a run that does not end ------------------- */
/* Appends to buf[at..n) what the wait core knows about thread `t` now and returns the new
 * length (the text is always NUL-terminated and never overruns; a full buffer truncates):
 *
 *   "  wait core: WAITING for <ms> ms so far for <its | any of | all of> <n> object(s) (<no timeout |
 *    timeout <ms> ms>[, alertable]); the wait began after signal #<S> of the process"
 *   then one line per object of the wait, in the wait's order:
 *   "    [<i>] <kind> <address>[ "<name>"]: <SIGNALLED | not signalled> for this thread now;
 *    <q> thread(s) queued on it; <never signalled | signalled <c> time(s), last by thread 0x<id>
 *    as signal #<s>, <AFTER | before> this wait began>"
 *   "  wait core: IN Sleep for <ms> ms so far (...)"            a Sleep or SleepEx
 *   "  wait core: not waiting ..."                              anything else
 *
 * A SIGNAL is one call of rt_signal: every change of an object that can end a wait (SetEvent,
 * ReleaseSemaphore, ReleaseMutex, a thread's end ...).  The process numbers them from 1, each
 * object remembers the number and the guest thread of its last one (thread 0: a host thread)
 * and each wait the number that was current when it joined its queues, so the report can say
 * on which side of the wait's beginning the last signal fell.  A thread that is still
 * queued on an object that reads SIGNALLED for it, in a wait for one object or for any of
 * several, is a wake-up the wait core lost: that line is the evidence.
 *
 * Meant for a signal handler (the driver's time limit) as much as for a test: no allocation,
 * no floating point, no lock taken for good.  Call rt_wait_report_lock first: it tries the
 * dispatcher lock for up to 200 ms and returns true if it took it; false means the calling
 * thread already holds it (it was interrupted inside) or it could not be had, and the report
 * then reads without it and says so in its first line.  rt_wait_report_unlock releases what
 * rt_wait_report_lock took and does nothing otherwise.  One reporter at a time.
 * `t` may be any RtThread, hand-made ones of the tests included; never NULL in earnest
 * (a NULL gives one line that says so). */
bool   rt_wait_report_lock(void);
void   rt_wait_report_unlock(void);
size_t rt_wait_report_thread(char *buf, size_t n, size_t at, RtThread *t);
/* One line for the same report: "timed waits and sleeps that ran out: <n>; they ended on
 * average <a> and at most <m> microseconds after their deadline" (every wait and Sleep of the
 * process that ended by its timeout, measured on the runtime's clock when the thread ran
 * again: how late the host's timer and scheduler let a timed wait end).  No lock. */
size_t rt_wait_report_lateness(char *buf, size_t n, size_t at);

/* ---- looking at an object (tests, reports) -------------------------------------------------- */

typedef struct RtSyncState {
    uint32_t kind;              /* RT_K_EVENT, RT_K_MUTEX or RT_K_SEMAPHORE */
    uint32_t waiters;           /* rt_wait_queue_length */
    /* event */
    bool     manual_reset;
    bool     set;
    /* mutex */
    bool     owned;
    bool     abandoned;         /* unowned because its owner exited; cleared by the next wait that takes it */
    uint32_t owner_thread_id;   /* guest thread id of the owner, 0 if unowned */
    uint32_t recursion;         /* waits the owner has not yet matched with a ReleaseMutex; 0 if unowned */
    /* semaphore */
    LONG     count;
    LONG     maximum;
} RtSyncState;

/* A consistent snapshot of an event, mutex or semaphore (the fields of the other two kinds
 * are zero).  Takes nothing from the object and wakes nobody.  It takes the dispatcher lock
 * itself, and may also be called with the lock already held.  Any other kind is fatal. */
void rt_sync_query(RtObject *obj, RtSyncState *out);

/* ---- between core/wait.c and core/sync.c ---------------------------------------------------- */

/* Abandon every mutex `t` owns; returns how many.  Dispatcher lock held.  Called by
 * rt_wait_thread_exit only. */
unsigned rt_sync_abandon_owned(struct RtThread *t);

#endif /* RT_SYNC_H */
