/* Kernel-object handles and waiting (docs/RUNTIME.md section 4).
 *
 * Built: objects, the name space and the handle table in runtime/core/handle.c; the
 * dispatcher lock, rt_wait and rt_signal in runtime/core/wait.c.  The comments are the
 * specification.  The handle-level calls built on these (events, mutexes, semaphores,
 * WaitForSingleObject and its family, SleepEx, APCs, thread exit) are in rt/sync.h.
 *
 * LAST ERROR.  Every function here that can fail sets the calling thread's last error itself
 * (through rt_thread_set_last_error, or rt_set_last_error where it has a Ctx) and says which
 * codes; a shim returns the failure value and sets nothing.  None sets it on success.
 *
 * LOCKS.  The table and the name space each have a host lock that is held for a few
 * instructions and never across a call out of handle.c, so every function down to
 * rt_htab_live may be called with any lock held, the dispatcher lock included.  The one
 * thing to know is where an object is destroyed: the last rt_object_unref (or the close
 * that drops the last reference) calls ops->destroy on the calling thread, with whatever
 * locks that caller holds.
 *
 * One table maps a handle to a reference-counted object with a kind.  Files, events,
 * mutexes, semaphores, threads, the process, file-mapping objects, find-file state, pipes,
 * sockets and open registry keys are in the table.  Modules are not (an HMODULE is an
 * image base, rt/module.h) and heaps are not (a heap handle is a guest address, rt/heap.h).
 *
 * A handle value is 32 bits: (generation << 22) | (index << 2).  It is a multiple of 4,
 * never 0 and never negative.  Lookups use only the low 32 bits of the HANDLE and ignore
 * the low two, as Windows does.  The generation makes a stale handle fail instead of
 * naming a newer object.
 *
 * A subsystem that owns a kind defines its object as a struct whose FIRST member is an
 * RtObject, and an RtObjectOps for it:
 *
 *     typedef struct { RtObject obj; int fd; ... } RtFile;
 *     static const RtObjectOps file_ops = { .name = "file", .destroy = file_destroy };
 *     RtFile *f = (RtFile *)rt_object_new(RT_K_FILE, sizeof *f, &file_ops, NULL, NULL);
 *     HANDLE h = rt_handle_new(&f->obj);
 */
#ifndef RT_HANDLE_H
#define RT_HANDLE_H

#include "runtime.h"
#include "rt/wintypes.h"

struct RtThread;

typedef enum {
    RT_K_ANY = 0,                 /* rt_handle_ref only: accept every kind */
    RT_K_FILE = 1, RT_K_EVENT, RT_K_MUTEX, RT_K_SEMAPHORE, RT_K_THREAD, RT_K_PROCESS,
    RT_K_SECTION, RT_K_FIND, RT_K_PIPE, RT_K_SOCKET, RT_K_REGKEY,
    /* The kinds of the "user" table (rt/window.h, rt/gdi.h): never in the kernel table. */
    RT_K_WINDOW, RT_K_DC, RT_K_GDIOBJ, RT_K_HOOK,
    RT_K_MSGQUEUE,                /* a thread's message queue: waitable, never has a handle */
    RT_K_COUNT
} RtKind;

typedef struct RtObject RtObject;

typedef struct RtObjectOps {
    const char *name;             /* for logs: "event" */

    /* The last reference is gone.  Release what the object owns (descriptors, buffers).
     * The core frees the object's own memory afterwards.  May be NULL. */
    void (*destroy)(RtObject *obj);

    /* Waitable kinds only (NULL for the others; waiting on such an object fails with
     * ERROR_INVALID_HANDLE).  Both are called with the dispatcher lock held.
     *   is_signaled  would a wait by `thread` on this object be satisfied now?
     *                (event: set; mutex: unowned or owned by `thread`; semaphore: count > 0;
     *                thread and process: exited)
     *   consume      the wait by `thread` is being satisfied: take what a successful wait
     *                takes (auto-reset event: reset; semaphore: count - 1; mutex: make
     *                `thread` the owner or deepen its recursion).  May be NULL if nothing
     *                is taken (manual-reset event, thread).  It returns nothing: a mutex
     *                that was abandoned by its previous owner says so by calling
     *                rt_wait_report_abandoned() (rt/sync.h) from inside consume, and the
     *                wait then returns WAIT_ABANDONED_0 + index.
     * Either may run on another thread than `thread` (the one that called rt_signal).  They
     * must not block, take the dispatcher lock, call rt_signal or guest code, or change any
     * object but their own; is_signaled must change nothing at all (it is asked again after
     * every consume). */
    bool (*is_signaled)(RtObject *obj, struct RtThread *thread);
    void (*consume)(RtObject *obj, struct RtThread *thread);

    /* CloseHandle on a handle to this kind fails with ERROR_INVALID_HANDLE and is logged;
     * the kind has its own close function (find-file handles: FindClose, which calls
     * rt_handle_close_kind). */
    bool no_close_handle;
} RtObjectOps;

/* The common header.  Kind-specific state follows it in the same allocation. */
struct RtObject {
    uint32_t           kind;      /* RtKind */
    uint32_t           refs;      /* atomic; the object dies when it reaches 0 */
    const RtObjectOps *ops;
    WCHAR             *name;      /* NULL for an unnamed object; owned by the core */
    void              *waiters;   /* wait core: threads queued on this object (dispatcher lock) */
    /* wait core, for its report of a run that does not end (rt/sync.h, THE WAIT REPORT), all
     * three written by rt_signal under the dispatcher lock: */
    uint64_t           sig_seq;   /* the number of the last rt_signal for this object among all of the process; 0: never */
    uint32_t           sig_thread;/* the guest thread that made it (0: a host thread) */
    uint32_t           sig_count; /* how many times rt_signal was called for this object */
};

/* Called once by rt_init: create the empty kernel table and the name space.  Fatal if
 * called twice, or if the table cannot be allocated. */
void rt_handle_init(void);

/* Allocate a zeroed object of `size` bytes (at least sizeof(RtObject)) in host memory with
 * one reference, owned by the caller.
 *   name     NULL, or the object's name in the kernel-object name space, which all kinds
 *            share and which is local to this process.  If an object with this name
 *            exists and has the same kind, no object is created: the existing one is
 *            returned with one more reference and *existed is set to true (the caller then
 *            reports ERROR_ALREADY_EXISTS where Windows does).  If it exists with a
 *            different kind the result is NULL with last error ERROR_INVALID_HANDLE.
 *   existed  may be NULL when name is NULL.
 * A named object leaves the name space when it dies.
 * NULL with last error ERROR_NOT_ENOUGH_MEMORY if the host has no memory.  Fatal: a kind
 * that is not a kind, a size below sizeof(RtObject), no ops or ops without a name, a name
 * without `existed`, a name for a kind that has none (only RT_K_EVENT, RT_K_MUTEX,
 * RT_K_SEMAPHORE and RT_K_SECTION are named), and the name forms this runtime does not
 * handle (empty, longer than MAX_PATH, a backslash other than the "Local\" or "Global\"
 * prefix).  Names compare exactly, case included; "Local\X" is the name "X".
 *
 * A NAMED OBJECT IS VISIBLE BEFORE ITS CREATOR HAS FILLED IT IN.  The name goes into the
 * name space inside this call, so from the moment it returns with *existed == false another
 * thread's rt_object_new or rt_object_open with the same name finds the object: zeroed
 * after the RtObject header, not yet what the creator means it to be.  Nothing in handle.c
 * prevents that.  The unit that owns a named kind therefore:
 *   - picks one lock for the kind (for a waitable kind the dispatcher lock, which waiting
 *     and signalling take anyway);
 *   - calls rt_object_new AND initialises the kind's fields in one critical section of it
 *     (rt_object_new may be called with that lock held: see LOCKS at the top);
 *   - takes the same lock in the kind's Open function around rt_object_open, and wherever
 *     it reads the fields of an object it found by name (*existed == true).
 * core/sync.c does this for events, mutexes and semaphores; the file unit must for
 * RT_K_SECTION.  An unnamed object is private to its creator until rt_handle_new. */
RtObject *rt_object_new(RtKind kind, size_t size, const RtObjectOps *ops,
                        const WCHAR *name, bool *existed);

/* Find a named object without creating one (OpenEvent, OpenFileMapping): the object with
 * one more reference, or NULL with last error ERROR_FILE_NOT_FOUND (no such name),
 * ERROR_INVALID_HANDLE (the name belongs to another kind) or ERROR_INVALID_PARAMETER (name
 * is NULL; Wine kernel32/tests/sync.c test_mutex, test_event, test_semaphore).  Fatal for a kind that has no
 * name and for the name forms rt_object_new refuses.  See the rule above for the lock the
 * caller holds. */
RtObject *rt_object_open(RtKind kind, const WCHAR *name);

/* One more reference, for a caller that already holds one; drop one.  The last unref
 * removes the name, calls ops->destroy and frees the object, on the calling thread.  Fatal:
 * NULL, a pointer that is not a live object of rt_object_new, one unref too many, and a
 * last unref while threads are still queued on the object (a waiting thread must hold a
 * reference). */
void rt_object_ref(RtObject *obj);
void rt_object_unref(RtObject *obj);

/* Put the object in the table and return its handle.  Consumes the caller's reference:
 * the table slot now holds it.  NULL with ERROR_NOT_ENOUGH_MEMORY if the table is full
 * (the reference is dropped). */
HANDLE rt_handle_new(RtObject *obj);

/* The object behind `h`, with one more reference that the caller drops with
 * rt_object_unref.  `kind` RT_K_ANY accepts every kind.  The pseudo-handles (HANDLE)-1
 * (current process) and (HANDLE)-2 (current thread) resolve to the real objects wherever
 * `kind` allows.  On failure (0, a free slot, a stale generation, the wrong kind) returns
 * NULL and sets last error ERROR_INVALID_HANDLE. */
RtObject *rt_handle_ref(HANDLE h, RtKind kind);

/* CloseHandle: drop the slot and its reference.  FALSE with ERROR_INVALID_HANDLE for a
 * handle that is not open, and for a kind whose ops say no_close_handle (logged).  Closing
 * a pseudo-handle returns TRUE and does nothing. */
BOOL rt_handle_close(HANDLE h);

/* The close function for kinds with no_close_handle: as rt_handle_close, but succeeds only
 * if the handle is of exactly `kind`. */
BOOL rt_handle_close_kind(HANDLE h, RtKind kind);

/* DuplicateHandle within the process: a new slot for the same object.  On a pseudo-handle
 * it makes a real handle.  NULL on failure, with last error ERROR_INVALID_HANDLE (`h` is
 * not open, or is of a kind whose ops say no_close_handle; logged) or
 * ERROR_NOT_ENOUGH_MEMORY (the table is full). */
HANDLE rt_handle_dup(HANDLE h);

/* THE PSEUDO-HANDLES.  (HANDLE)-1 and (HANDLE)-2 are recognised by their full 64-bit value
 * (the sign-extended -1 and -2; a zero-extended 0xFFFFFFFF is an ordinary invalid handle).
 * (HANDLE)-2 resolves to the calling thread's own thread object, which the thread unit keeps
 * in RtThread.sub[RT_SUB_HANDLE] (the rule is in rt/thread.h); (HANDLE)-1 resolves to the
 * object registered here.  Using either before its object exists, or (HANDLE)-2 on a host
 * thread that is not a guest thread, is a fatal report, not an error return.
 *
 * Register the one RT_K_PROCESS object.  Called once, by the unit that makes the process
 * object (the thread unit), before guest code runs.  The table takes its own reference;
 * the caller keeps its own.  Fatal: before rt_handle_init, an object that is not of kind
 * RT_K_PROCESS, a second call.  Sets no last error. */
void rt_handle_set_process_object(RtObject *process);

/* ---- a handle table as a value ----------------------------------------------------------- */
/* The kernel table behind rt_handle_* is one instance of RtHandleTable, made by
 * rt_handle_init with capacity 1,048,576, min_free 1,024 and generations 1 to 255.  A
 * second instance is planned for window handles (RUNTIME.md 4.1), with generations 256 to
 * 511: because the ranges do not overlap, a handle of one table never names a slot of the
 * other, and mixing the two spaces fails with ERROR_INVALID_HANDLE instead of naming the
 * wrong object.  The name space of rt_object_new is shared by all tables; the
 * pseudo-handles belong to the kernel table only and mean nothing to rt_htab_*.
 *
 * Each table has its own lock, taken inside these functions and never held across a call
 * out of handle.c; all of them may be called from any thread. */

typedef struct RtHandleTable RtHandleTable;

typedef struct RtHandleTableConfig {
    const char *name;           /* for reports and logs: "kernel", "window".  Not copied: it
                                   must stay valid for as long as the table does */
    uint32_t    capacity;       /* slots, 1 to 1,048,576.  16 bytes of host memory each,
                                   allocated at once and never moved */
    uint32_t    min_free;       /* a freed slot is handed out again only while at least this
                                   many freed slots are waiting (oldest first), or when no
                                   never-used slot is left.  0 and 1: the next rt_htab_new
                                   reuses the oldest freed slot */
    uint32_t    gen_lo, gen_hi; /* the generations this table uses, 1 <= gen_lo <= gen_hi <=
                                   511.  A slot starts at gen_lo, advances by one each time
                                   it is freed and wraps from gen_hi to gen_lo */
} RtHandleTableConfig;

/* A new, empty table.  Fatal: cfg or cfg->name NULL, a capacity or a generation range
 * outside the limits above, no host memory.  Never returns NULL; sets no last error. */
RtHandleTable *rt_htab_create(const RtHandleTableConfig *cfg);

/* Drop the reference of every occupied slot (destroying objects whose last reference it
 * was) and free the table.  The caller guarantees that no other thread is using `t`.
 * Fatal for NULL. */
void rt_htab_destroy(RtHandleTable *t);

/* As rt_handle_new on table `t`: the handle, or NULL with ERROR_NOT_ENOUGH_MEMORY if every
 * slot is occupied (logged; the caller's reference is dropped).  Fatal: NULL table, an
 * object that is not a live object of rt_object_new. */
HANDLE rt_htab_new(RtHandleTable *t, RtObject *obj);

/* As rt_handle_ref on table `t`, without the pseudo-handles: (HANDLE)-1 and (HANDLE)-2 are
 * invalid handles here.  `kind` is an RtKind; RT_K_ANY (0) accepts every kind.  NULL with
 * ERROR_INVALID_HANDLE for 0, an index beyond the capacity, a free slot, a generation
 * that is not the slot's current one, or the wrong kind. */
RtObject *rt_htab_ref(RtHandleTable *t, HANDLE h, uint32_t kind);

/* Empty the slot and drop its reference.  `kind` RT_K_ANY (0) is CloseHandle: it refuses a
 * kind whose ops say no_close_handle (logged).  Any other `kind` closes only a handle of
 * exactly that kind, no_close_handle or not.  TRUE, or FALSE with ERROR_INVALID_HANDLE
 * (not open, wrong kind, refused); nothing is changed then. */
BOOL rt_htab_close(RtHandleTable *t, HANDLE h, uint32_t kind);

/* Number of occupied slots now.  For tests and reports. */
uint32_t rt_htab_live(RtHandleTable *t);

/* ---- waiting (section 4.2) ------------------------------------------------------------------ */
/* One dispatcher lock and one wait core serve WaitForSingleObject, WaitForMultipleObjects,
 * the Ex forms, SignalObjectAndWait, SleepEx and (when it is built)
 * MsgWaitForMultipleObjects.  The hot locks of the engine (critical sections, SRW locks,
 * SList) never come here (rt/lock.h).  A shim calls the handle-level functions of
 * rt/sync.h; what is declared here is for a unit that defines a waitable kind. */

/* The dispatcher lock: one lock for the signalled state of every waitable object, every
 * wait queue and every thread's wait record.  A kind's code takes it to change an object's
 * signalled state and calls rt_signal before releasing it:
 *
 *     rt_dispatcher_lock();  ev->set = true;  rt_signal(&ev->obj);  rt_dispatcher_unlock();
 *
 * It is a host os_unfair_lock: not fair, NOT RECURSIVE, and it knows whether the calling
 * host thread holds it (rt_dispatcher_held, rt/sync.h).  Taking it while holding it, and
 * releasing it without holding it, are fatal reports.  Hold it for a few instructions:
 * never across a guest call, a blocking host call, a sleep or a log line.  Allocating (and
 * so rt_object_new, see the rule for named objects above) is allowed under it.  Neither
 * function needs a guest thread or sets last error. */
void rt_dispatcher_lock(void);
void rt_dispatcher_unlock(void);

/* Wait for one of (or all of) `n` objects.  The caller holds a reference to each object
 * for the duration, and does NOT hold the dispatcher lock (the function takes it).
 *   ctx         the calling guest thread's; a thread waits only for itself
 *   n, objs     1 <= n <= MAXIMUM_WAIT_OBJECTS (64).  A wait-any may name one object more
 *               than once (the lowest index is reported); a wait-all may not
 *   wait_all    satisfy only when every object is signalled for this thread at once;
 *               they are observed and consumed in one step under the dispatcher lock, so a
 *               wait-all never holds one object while it waits for another
 *   timeout_ms  INFINITE, or milliseconds (not rounded to a tick); 0 polls
 *   alertable   also end the wait when an APC is queued to the thread (rt_apc_queue,
 *               rt/sync.h): the thread then runs EVERY queued APC, those queued meanwhile
 *               included, through rt_guest_call with no lock held, and returns
 *               WAIT_IO_COMPLETION having taken nothing from any object
 * Order of the checks on entry (the NT kernel's as far as known; not verified on Windows):
 * the objects (a wait that can be satisfied is, and queued APCs stay queued); then, if
 * alertable and an APC is queued, WAIT_IO_COMPLETION, even with timeout 0; then timeout 0
 * gives WAIT_TIMEOUT; otherwise the thread is queued on every object, first in first out,
 * and sleeps.  It sleeps on a 32-bit word of its own wait record
 * (os_sync_wait_on_address, the primitive of rt/lock.h), not on a condition variable; the
 * record is in RtThread.sub[RT_SUB_WAIT], allocated at the thread's first wait.  The
 * deadline is on the runtime's monotonic clock (rt_time_mono_100ns), fixed when the thread
 * is queued; WAIT_TIMEOUT is never returned before that clock has reached it.  If the
 * timeout runs out as a signal arrives, the wait reports the signal: whatever a signaller
 * took on the thread's behalf is never lost.
 *
 * Returns
 *   WAIT_OBJECT_0 + i     satisfied by objs[i], the lowest satisfiable index (wait-any);
 *                         WAIT_OBJECT_0 for a wait-all
 *   WAIT_ABANDONED_0 + i  as above (i is 0 for a wait-all), and the object taken (for a
 *                         wait-all: any of them) was a mutex whose owner exited while
 *                         holding it: its consume called rt_wait_report_abandoned.  The
 *                         mutex is acquired all the same
 *   WAIT_TIMEOUT          the time ran out and nothing was taken
 *   WAIT_IO_COMPLETION    alertable, and the queued APCs were run; nothing was taken
 *   WAIT_FAILED           with last error set on `ctx`; nothing was taken or queued:
 *       ERROR_INVALID_PARAMETER  n is 0 or more than 64; a wait-all names an object twice
 *                                (two handles to one object count as twice)
 *       ERROR_INVALID_HANDLE     an object's kind has no is_signaled operation (Windows may
 *                                report ERROR_ACCESS_DENIED for registry keys and sections;
 *                                not verified)
 * A sleep is not a wait with n == 0: SleepEx is rt_sleep_ex (rt/sync.h).
 * Fatal: no ctx or a ctx with no RtThread; objs NULL with n in range; a NULL object; a
 * wait by a thread for which rt_wait_thread_exit was called; calling it with the
 * dispatcher lock held (so also from is_signaled or consume). */
DWORD rt_wait(Ctx *ctx, unsigned n, RtObject *const *objs, bool wait_all, DWORD timeout_ms, bool alertable);

/* `obj` may have become signalled.  Called with the dispatcher lock held, after changing
 * the object's state, and before the lock is released (that order is what makes a signal
 * impossible to lose).  Walks the object's queue in the order the threads began to wait
 * and, for each whose WHOLE wait can now be satisfied, consumes on its behalf, sets its
 * result and wakes it; the object is asked again (is_signaled) for each entry, so an
 * auto-reset event or one unit of a semaphore releases one thread and a manual-reset event
 * releases all.  An entry that cannot be satisfied (a wait-all still missing another
 * object, a mutex owned by someone else) is skipped, not a stop.  Wakes nobody if nobody
 * can be satisfied.  Waking is a system call made under the lock.
 * Fatal: called without the lock, with NULL, or from a consume operation.  Sets no last
 * error and needs no guest thread. */
void rt_signal(RtObject *obj);

#endif /* RT_HANDLE_H */
