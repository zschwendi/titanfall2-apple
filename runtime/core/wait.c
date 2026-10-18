/* Waiting on kernel objects (docs/RUNTIME.md section 4.2).  Interface and specification:
 * rt/handle.h (the dispatcher lock, rt_wait, rt_signal) and rt/sync.h (the rest).
 *
 * WHAT IS HERE
 *
 *   the dispatcher lock   one lock for the signalled state of every waitable object, every
 *                         wait queue and every thread's wait record.
 *   rt_wait               check under the lock whether the wait can be satisfied; if not,
 *                         queue the thread on every object and sleep.
 *   rt_signal             called by a kind, under the lock, after it changed an object:
 *                         satisfy the queued waits that can now be satisfied, in queue order,
 *                         taking from the objects on the waiters' behalf.
 *   APCs                  a per-thread queue of guest calls that an alertable wait runs.
 *   thread exit           rt_wait_thread_exit.
 *
 * This file knows nothing about any kind.  Events, mutexes and semaphores are core/sync.c;
 * threads and the process belong to the thread unit.  A kind is its two operations,
 * is_signaled and consume (rt/handle.h).
 *
 * THE LOCK.  An os_unfair_lock, not a pthread mutex.  Measured on this machine (M3 Pro, the
 * machine busy with other builds), 8 threads each taking and releasing the lock 20,000 times
 * around a few instructions (160,000 calls):
 *
 *     pthread mutex, default (fair) policy      561 ms and 830 ms in two runs
 *     pthread mutex, first-fit policy           3.4 ms and 4.8 ms
 *     os_unfair_lock                            1.8 ms and 5.8 ms
 *
 * The fair mutex hands itself from waiter to waiter, each of which must be scheduled before
 * the next can run; the other two let a running thread take a free lock.  The unfair lock was
 * chosen over the first-fit mutex because it also lends the owner the priority of a waiter,
 * and because nothing here needs a pthread condition variable (see HOW A THREAD SLEEPS).
 * The lock is not recursive.  This file records per host thread whether the thread holds it,
 * so that taking it twice, releasing it unheld and calling rt_signal without it are fatal
 * reports that say so instead of a hang or a corrupted queue.
 *
 * The lock is held for a few instructions at a time: never across a sleep, a guest call, a
 * blocking host call or logging.  Two things are done under it that are system calls: waking
 * a sleeping thread (see below) and, in the creation functions of core/sync.c, allocating an
 * object.
 *
 * QUEUES.  A thread in a wait that could not be satisfied at once has one entry (WaitBlock)
 * per object it named, appended to that object's queue (RtObject.waiters).  The entries live
 * in the thread's wait record, so queueing allocates nothing.  A queue is first-in first-out:
 * rt_signal looks at the entries in the order the threads started waiting.  RtObject.waiters
 * is NULL exactly when nobody is queued (the handle unit refuses to destroy an object with a
 * queue).  A waiting thread must hold a reference to each object for as long as it waits;
 * rt/handle.h makes that the caller's duty and the handle-level calls of core/sync.c do it.
 *
 * SATISFYING A WAIT.  One function, try_satisfy, decides and takes, always under the lock and
 * always for the whole wait: a wait-any takes the lowest-indexed object that is signalled
 * for the thread; a wait-all takes every object, and only when all of them are signalled for
 * the thread at once.  Nothing is taken otherwise.  It runs in two places: in rt_wait before
 * the thread sleeps, and in rt_signal on behalf of each queued thread.  That is why wait-all
 * is atomic (two threads waiting for the same two mutexes in opposite orders cannot deadlock:
 * whichever is examined first when both mutexes are free takes both, and neither ever owns
 * one while queued for the other), and why a signal is never lost: an object's state changes
 * only under the lock, and the change is followed by rt_signal before the lock is released,
 * so a thread whose whole wait could be satisfied is never still queued when the lock is
 * released.  (A wait-all can stay queued on an object that is signalled, for as long as
 * another of its objects is not.)
 *
 * HOW A THREAD SLEEPS.  On a 32-bit word of its own wait record, `wake`, with the same
 * primitive the embedded locks use (os_sync_wait_on_address, core/lock.c), instead of the
 * condition variable RUNTIME.md 4.2 names: a condition variable needs a pthread mutex, and
 * the lock is not one.
 *
 *     the waiter, under the lock:    queue the entries; waiting = true; wake = 0; unlock;
 *                                    then: while (wake == 0) sleep-while(wake == 0)
 *     whoever ends the wait, under the lock:
 *                                    dequeue the entries; waiting = false; result = ...;
 *                                    wake = 1; wake the sleeper
 *
 * Lost wake-ups.  The kernel compares the word with 0 and puts the thread to sleep as one
 * step with respect to wake calls on that word.  The word is set to 1 before the wake call,
 * so either the sleeper is already asleep when the wake call comes and is woken, or it reads
 * 1 and does not sleep.  Only the thread itself sets the word to 0, under the lock, at the
 * start of a wait, and whoever ends a wait does both the store and the wake call before it
 * releases the lock: so no wake of an earlier wait can arrive during a later one.  The sleep
 * may also return for no reason; the loop reads the word again.
 *
 * Memory ordering (arm64, no x86 ordering mode in tests or on host threads).  Everything
 * shared is read and written under the lock, with two exceptions.  The word is an atomic,
 * loaded and stored sequentially consistent.  The result of a wait is written under the lock
 * before the word is set to 1 and read by the waiter after it has loaded a nonzero word: the
 * atomic store and load order the two.  A waiter that timed out reads its result under the
 * lock instead.
 *
 * TIMEOUTS run on the runtime's monotonic clock, rt_time_mono_100ns (rt/time.h): the deadline
 * is the clock's value when the thread joins the queues plus the timeout.  That clock counts
 * through host sleep.  As this unit reads the documentation of the wait functions, so does
 * Windows 7 (before Windows 8 the timeout "does include time spent in low-power states", and
 * from Windows 8 on it does not: Microsoft's WaitForSingleObject page, read 2026-10-10,
 * docs/reviews/win-behaviour-check-2.md).
 * The thread sleeps on the host for the time still missing and then reads the runtime clock
 * again, so a wait returns WAIT_TIMEOUT only once the RUNTIME clock has reached the deadline,
 * never earlier.  Two consequences:
 *   - The host sleep is measured on a clock that stops while the machine sleeps (the only one
 *     the primitive offers), so after the machine wakes, a wait can last longer than its
 *     timeout by up to the time that was still missing when the machine went to sleep.
 *   - When rt/time.h is backed by a test clock (rt_time_set_source), a sleeping wait follows
 *     it only at its own wake-ups: with a
 *     clock that stands still it never times out; with one that jumps forward it notices the
 *     jump when the host sleep it had started ends.  A test clock that wants waits to react
 *     at once needs a call that wakes the sleepers; none exists.
 * A wait whose timeout runs out at the moment a signal arrives is decided under the lock: if
 * the signaller got there first the object was taken on the thread's behalf and the wait
 * returns that result, never WAIT_TIMEOUT, so nothing taken is ever lost.
 *
 * ALERTABLE WAITS.  rt_apc_queue appends to the target thread's queue and, if the thread is
 * sleeping in an alertable wait, ends that wait with WAIT_IO_COMPLETION (nothing is taken).
 * The thread then runs the queued calls itself through rt_guest_call, with the lock released,
 * until the queue is empty, and returns WAIT_IO_COMPLETION.  Order of the checks at the start
 * of a wait, which is the NT kernel's as far as it could be established without a Windows
 * machine (from the ReactOS and Windows Research Kernel wait routines; UNSURE):
 *     1. the objects: if the wait can be satisfied, it is, and queued APCs stay queued;
 *     2. alertable and an APC is queued: run them, WAIT_IO_COMPLETION, even with timeout 0;
 *     3. timeout 0: WAIT_TIMEOUT;
 *     4. sleep.
 *
 * FATAL REPORTS.  A misuse of the interface by core code (a NULL object, rt_signal without
 * the lock, a wait by a thread that has exited) ends in rt_fatal.  Argument errors that
 * Windows reports to the caller are returned (see rt_wait).
 */
#include <errno.h>
#include <os/lock.h>
#include <os/os_sync_wait_on_address.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "rt/handle.h"
#include "rt/sync.h"
#include "rt/test_fork.h"
#include "rt/time.h"

/* ------------------------------------------------------------------------------------------ */
/* Types                                                                                      */
/* ------------------------------------------------------------------------------------------ */

typedef struct WaitState WaitState;

/* One thread's place in one object's queue.  The queue is a doubly linked list whose head is
 * RtObject.waiters: `next` runs towards the tail and is NULL at the tail; `prev` runs towards
 * the head, and the head's `prev` is the tail (so appending needs no walk and no second
 * pointer in the object). */
typedef struct WaitBlock {
    struct WaitBlock *next, *prev;
    WaitState        *ws;               /* the waiting thread's record */
    RtObject         *obj;
} WaitBlock;

/* A queued asynchronous procedure call: fn(args[0], ..., args[nargs - 1]). */
typedef struct Apc {
    struct Apc *next;
    GUESTFN     fn;
    unsigned    nargs;                  /* 1 to RT_APC_MAX_ARGS */
    uint64_t    args[RT_APC_MAX_ARGS];
} Apc;

/* Per-thread state: RtThread.sub[RT_SUB_WAIT].  Created on the thread's first wait (or by
 * the first APC queued to it), freed by rt_wait_thread_exit.  Everything except `wake` and
 * `result` is guarded by the dispatcher lock (see the top of the file for those two). */
struct WaitState {
    RtThread *thread;
    uint32_t  wake;                     /* the word the thread sleeps on: 0 sleep, 1 the wait is over */
    DWORD     result;                   /* what the wait returns, set by whoever ended it */
    bool      waiting;                  /* queued: blocks[0..n) are in their objects' queues */
    bool      wait_all, alertable;      /* of the current wait */
    unsigned  n;                        /* objects in the current wait */
    DWORD     timeout_ms;               /* of the current wait: for the wait report only */
    uint64_t  since;                    /* rt_time_mono_100ns() when the thread joined the queues: for the wait report only */
    uint64_t  since_seq;                /* signal_seq at that moment: for the wait report only */
    Apc      *apc_head, *apc_tail;      /* queued APCs, oldest first */
    WaitBlock blocks[MAXIMUM_WAIT_OBJECTS];     /* blocks[i].obj is object i of the current wait */
};

/* What RtThread.sub[RT_SUB_WAIT] points to after rt_wait_thread_exit.  Never read or written. */
static WaitState exited_marker;

/* ------------------------------------------------------------------------------------------ */
/* The dispatcher lock                                                                        */
/* ------------------------------------------------------------------------------------------ */

static os_unfair_lock       disp_lock = OS_UNFAIR_LOCK_INIT;
static _Thread_local bool   disp_held;          /* does this host thread hold disp_lock? */

void rt_dispatcher_lock(void) {
    if (disp_held)
        rt_fatal("rt_dispatcher_lock: the calling thread already holds the dispatcher lock.  It is not "
                 "recursive: an is_signaled or consume operation, or code between rt_dispatcher_lock and "
                 "rt_dispatcher_unlock, called something that takes it");
    os_unfair_lock_lock(&disp_lock);
    disp_held = true;
}

void rt_dispatcher_unlock(void) {
    if (!disp_held) rt_fatal("rt_dispatcher_unlock: the calling thread does not hold the dispatcher lock");
    disp_held = false;
    os_unfair_lock_unlock(&disp_lock);
}

bool rt_dispatcher_held(void) { return disp_held; }

/* Test only (rt/test_fork.h): in a forked child, whose copy of the lock may be held by a
 * thread that does not exist there. */
void rt_dispatcher_forked_child(void) {
    disp_lock = OS_UNFAIR_LOCK_INIT;
    disp_held = false;
}

/* ------------------------------------------------------------------------------------------ */
/* Sleeping and waking                                                                        */
/* ------------------------------------------------------------------------------------------ */
/* os_sync_wait_on_address and os_sync_wake_by_address_any (macOS 14.4), size 4, on
 * WaitState.wake.  What the kernel does, as core/lock.c found on this machine and a probe for
 * this unit confirmed: a wait whose value does not match returns at once; a wake with nobody
 * asleep fails with ENOENT; a timed wait that runs out fails with ETIMEDOUT; a timeout of 0 ns
 * is EINVAL (never passed here); a timeout of 2^64 - 1 ns is accepted; a wait can fail with
 * EFAULT when the word's page is not resident and the caller is expected to touch it. */

/* Sleep while ws->wake is 0, for at most `ns` host nanoseconds (0: no limit).  May return
 * early for no reason: the caller loops. */
static void sleep_on_word(WaitState *ws, uint64_t ns) {
    int r = ns ? os_sync_wait_on_address_with_timeout(&ws->wake, 0, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE,
                                                      OS_CLOCK_MACH_ABSOLUTE_TIME, ns)
               : os_sync_wait_on_address(&ws->wake, 0, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE);
    if (r >= 0) return;
    int err = errno;
    switch (err) {
    case ETIMEDOUT:                     /* the caller reads the runtime clock and decides */
    case EINTR:                         /* interrupted: a spurious return */
        return;
    case EFAULT:                        /* not resident: touch the page and come back */
        (void)__atomic_load_n(&ws->wake, __ATOMIC_RELAXED);
        return;
    case ENOMEM:                        /* the kernel could not allocate its queue entry: try again */
        sched_yield();
        return;
    default:
        rt_fatal("wait core: os_sync_wait_on_address on the wait word of thread 0x%x failed with errno %d",
                 ws->thread->id, err);
    }
}

/* Dispatcher lock held.  End the sleep of the thread that owns `ws`.  The wake call is made
 * under the lock so that it cannot outlive the wait record (the thread may exit as soon as
 * it runs) and cannot arrive during the thread's next wait. */
static void wake_thread(WaitState *ws) {
    __atomic_store_n(&ws->wake, 1, __ATOMIC_SEQ_CST);
    for (;;) {
        if (os_sync_wake_by_address_any(&ws->wake, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE) == 0) return;
        int err = errno;
        if (err == ENOENT) return;      /* not asleep (yet): it reads the word before it sleeps */
        if (err == EINTR) continue;
        if (err == ENOMEM) { sched_yield(); continue; }
        rt_fatal("wait core: os_sync_wake_by_address_any on the wait word of thread 0x%x failed with errno %d",
                 ws->thread->id, err);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Queues                                                                                     */
/* ------------------------------------------------------------------------------------------ */

/* Dispatcher lock held.  Append to the tail of b->obj's queue. */
static void queue_append(WaitBlock *b) {
    RtObject *o = b->obj;
    WaitBlock *head = o->waiters;
    b->next = NULL;
    if (!head) {
        b->prev = b;                    /* the head's prev is the tail: itself */
        o->waiters = b;
        return;
    }
    WaitBlock *tail = head->prev;
    tail->next = b;
    b->prev = tail;
    head->prev = b;
}

/* Dispatcher lock held.  Unlink from b->obj's queue. */
static void queue_remove(WaitBlock *b) {
    RtObject *o = b->obj;
    WaitBlock *head = o->waiters;
    if (b == head) {
        WaitBlock *new_head = b->next;
        if (new_head) new_head->prev = b->prev;         /* the tail, which may be new_head itself */
        o->waiters = new_head;
    } else {
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
        else head->prev = b->prev;                      /* b was the tail */
    }
    b->next = b->prev = NULL;
}

unsigned rt_wait_queue_length(RtObject *obj) {
    if (!obj) rt_fatal("rt_wait_queue_length: NULL object");
    unsigned n = 0;
    bool held = disp_held;
    if (!held) rt_dispatcher_lock();
    for (const WaitBlock *b = obj->waiters; b; b = b->next) n++;
    if (!held) rt_dispatcher_unlock();
    return n;
}

/* ------------------------------------------------------------------------------------------ */
/* Per-thread state                                                                           */
/* ------------------------------------------------------------------------------------------ */

/* Dispatcher lock held.  The thread's wait record, created if it has none; NULL if the
 * thread has exited. */
static WaitState *state_of(RtThread *t) {
    WaitState *ws = t->sub[RT_SUB_WAIT];
    if (ws == &exited_marker) return NULL;
    if (ws) return ws;
    ws = calloc(1, sizeof *ws);
    if (!ws) rt_fatal("wait core: no memory for the wait record of thread 0x%x (%zu bytes)", t->id, sizeof *ws);
    ws->thread = t;
    for (unsigned i = 0; i < MAXIMUM_WAIT_OBJECTS; i++) ws->blocks[i].ws = ws;
    t->sub[RT_SUB_WAIT] = ws;
    return ws;
}

/* ------------------------------------------------------------------------------------------ */
/* Satisfying a wait                                                                          */
/* ------------------------------------------------------------------------------------------ */

static uint64_t late_count, late_total, late_max;   /* timed waits and sleeps that ran out, and by how much (100 ns) past their deadline: rt_wait_report_lateness */
static uint64_t signal_seq;             /* calls of rt_signal so far (dispatcher lock): RtObject.sig_seq, WaitState.since_seq */
static bool in_consume;                 /* a consume operation is running (dispatcher lock) */
static bool consume_abandoned;          /* ... and it called rt_wait_report_abandoned */

void rt_wait_report_abandoned(void) {
    if (!disp_held || !in_consume)
        rt_fatal("rt_wait_report_abandoned: called outside a consume operation.  Only the consume operation of a "
                 "kind may call it, while the wait core is satisfying a wait");
    consume_abandoned = true;
}

/* Dispatcher lock held.  The wait by `t` on `o` is being satisfied: let the kind take what a
 * successful wait takes.  True if the kind says the object had been abandoned. */
static bool consume(RtObject *o, RtThread *t) {
    if (!o->ops->consume) return false;         /* nothing is taken (manual-reset event, thread) */
    in_consume = true;
    consume_abandoned = false;
    o->ops->consume(o, t);
    in_consume = false;
    return consume_abandoned;
}

/* Dispatcher lock held.  If the current wait of `ws` can be satisfied now, satisfy it (take
 * from the objects) and store what the wait returns in *result. */
static bool try_satisfy(WaitState *ws, DWORD *result) {
    RtThread *t = ws->thread;
    if (!ws->wait_all) {
        /* Rule (WaitForMultipleObjects, return value): "If more than one object became signaled
         * during the call, this is the array index of the signaled object with the smallest
         * index value of all the signaled objects."  Only that object is taken.
         * Rule: WAIT_ABANDONED_0 + index when the object taken is an abandoned mutex. */
        for (unsigned i = 0; i < ws->n; i++) {
            RtObject *o = ws->blocks[i].obj;
            if (!o->ops->is_signaled(o, t)) continue;
            *result = (consume(o, t) ? WAIT_ABANDONED_0 : WAIT_OBJECT_0) + i;
            return true;
        }
        return false;                           /* also: n == 0, a sleep, which only time or an APC ends */
    }
    /* Rule (bWaitAll): "the function returns when the state of all objects in the lpHandles
     * array is signaled", and "the function does not modify the states of the specified
     * objects until the states of all objects have been set to signaled".  Nothing is taken
     * unless everything can be. */
    for (unsigned i = 0; i < ws->n; i++) {
        RtObject *o = ws->blocks[i].obj;
        if (!o->ops->is_signaled(o, t)) return false;
    }
    /* Rule: "If bWaitAll is TRUE, the return value indicates that the state of all specified
     * objects is signaled and at least one of the objects is an abandoned mutex object":
     * WAIT_ABANDONED_0 with no index (kernel: STATUS_ABANDONED), else WAIT_OBJECT_0. */
    bool abandoned = false;
    for (unsigned i = 0; i < ws->n; i++)
        if (consume(ws->blocks[i].obj, t)) abandoned = true;
    *result = abandoned ? WAIT_ABANDONED_0 : WAIT_OBJECT_0;
    return true;
}

/* Dispatcher lock held.  Take the thread out of every queue it is in. */
static void dequeue_all(WaitState *ws) {
    for (unsigned i = 0; i < ws->n; i++) queue_remove(&ws->blocks[i]);
    ws->waiting = false;
}

/* Dispatcher lock held.  End the sleeping wait of `ws` with `result`. */
static void finish_wait(WaitState *ws, DWORD result) {
    dequeue_all(ws);
    ws->result = result;
    wake_thread(ws);
}

void rt_signal(RtObject *obj) {
    if (!disp_held)
        rt_fatal("rt_signal: called without the dispatcher lock.  Change the object's state and call rt_signal "
                 "between rt_dispatcher_lock and rt_dispatcher_unlock");
    if (!obj) rt_fatal("rt_signal: NULL object");
    if (in_consume) rt_fatal("rt_signal: called from a consume operation, which may only take from its object");

    /* For the wait report (THE WAIT REPORT, below): who signalled this object last, and the
     * number of that signal among all signals of the process.  Three plain stores and one
     * thread-local read under the lock that is held anyway; no clock is read. */
    const RtThread *self = rt_thread_self();
    obj->sig_seq = ++signal_seq;
    obj->sig_thread = self ? self->id : 0;
    obj->sig_count++;

    /* Queue order.  After each wait that is satisfied the object is looked at afresh for the
     * next entry (try_satisfy asks is_signaled again), so an auto-reset event or a semaphore
     * with one unit releases one thread and a manual-reset event releases them all.  The walk
     * goes on past an entry that cannot be satisfied: whether an object is signalled depends
     * on the thread (a mutex), and a wait-all further down may need less than one further up. */
    WaitBlock *b = obj->waiters;
    while (b) {
        WaitState *ws = b->ws;
        /* Satisfying this wait unlinks every entry of the same thread, and a wait-any may name
         * the object twice: step over them now.  Entries of other threads are not touched. */
        WaitBlock *next = b->next;
        while (next && next->ws == ws) next = next->next;
        DWORD result;
        if (try_satisfy(ws, &result)) finish_wait(ws, result);
        b = next;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* APCs                                                                                       */
/* ------------------------------------------------------------------------------------------ */

/* No lock held.  Run the calling thread's queued APCs, oldest first, until the queue is empty:
 * the ones queued while these run included.
 * Rule (QueueUserAPC): "After the thread is in an alertable state, the thread handles all
 * pending APCs in first in, first out (FIFO) order, and the wait operation returns
 * WAIT_IO_COMPLETION."  Each is a guest function: `VOID CALLBACK fn(ULONG_PTR param)` for
 * rt_apc_queue, up to RT_APC_MAX_ARGS arguments for rt_apc_queue_args. */
static void run_apcs(Ctx *ctx, WaitState *ws) {
    for (;;) {
        rt_dispatcher_lock();
        Apc *a = ws->apc_head;
        if (a) {
            ws->apc_head = a->next;
            if (!a->next) ws->apc_tail = NULL;
        }
        rt_dispatcher_unlock();
        if (!a) return;
        const Apc call = *a;
        free(a);
        /* The call may wait alertably itself (it then runs the rest of the queue) or queue
         * more.  If it exits the thread it never comes back here. */
        rt_guest_call(ctx, call.fn, call.nargs, call.args);
    }
}

static void set_error(Ctx *ctx, DWORD e) {
    if (ctx) rt_set_last_error(ctx, e);
    else rt_thread_set_last_error(e);
}

BOOL rt_apc_queue(Ctx *ctx, RtThread *target, GUESTFN fn, ULONG_PTR param) {
    const uint64_t arg = param;
    return rt_apc_queue_args(ctx, target, fn, 1, &arg);
}

BOOL rt_apc_queue_args(Ctx *ctx, RtThread *target, GUESTFN fn, unsigned nargs, const uint64_t *args) {
    if (!target) rt_fatal("rt_apc_queue: NULL target thread");
    if (nargs < 1 || nargs > RT_APC_MAX_ARGS || !args)
        rt_fatal("rt_apc_queue_args: %u arguments at %p; an APC takes 1 to %u", nargs, (const void *)args, RT_APC_MAX_ARGS);
    /* A NULL function is queued like any other, as on Windows, and fails where it is called:
     * the guest call reports a call through a null function pointer. */
    Apc *a = malloc(sizeof *a);
    if (!a) {
        set_error(ctx, ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    a->next = NULL;
    a->fn = fn;
    a->nargs = nargs;
    for (unsigned i = 0; i < RT_APC_MAX_ARGS; i++) a->args[i] = i < nargs ? args[i] : 0;

    rt_dispatcher_lock();
    WaitState *ws = state_of(target);
    if (!ws) {
        rt_dispatcher_unlock();
        free(a);
        /* The kernel refuses to queue to a thread that has ended with STATUS_UNSUCCESSFUL, which
         * kernel32 reports as ERROR_GEN_FAILURE (Wine kernel32/tests/sync.c test_QueueUserAPC). */
        set_error(ctx, ERROR_GEN_FAILURE);
        return FALSE;
    }
    if (ws->apc_tail) ws->apc_tail->next = a;
    else ws->apc_head = a;
    ws->apc_tail = a;
    /* A thread sleeping in an alertable wait stops waiting: nothing is taken from its
     * objects, and it runs the queue when it wakes.  A thread in any other state finds the
     * APC at the start of its next alertable wait. */
    if (ws->waiting && ws->alertable) finish_wait(ws, WAIT_IO_COMPLETION);
    rt_dispatcher_unlock();
    return TRUE;
}

/* ------------------------------------------------------------------------------------------ */
/* Waiting                                                                                    */
/* ------------------------------------------------------------------------------------------ */

DWORD rt_wait_locked(Ctx *ctx, unsigned n, RtObject *const *objs, bool wait_all, DWORD timeout_ms, bool alertable) {
    if (!disp_held) rt_fatal("rt_wait_locked: called without the dispatcher lock");
    if (!ctx || !RT_THREAD(ctx)) rt_fatal("rt_wait: no ctx, or a ctx that belongs to no thread");
    if (n > MAXIMUM_WAIT_OBJECTS) rt_fatal("rt_wait_locked: %u objects; the limit is %u", n, MAXIMUM_WAIT_OBJECTS);
    RtThread *t = RT_THREAD(ctx);
    WaitState *ws = state_of(t);
    if (!ws)
        rt_fatal("rt_wait: thread 0x%x waits after rt_wait_thread_exit was called for it; a thread that has exited "
                 "cannot wait", t->id);
    if (ws->waiting) rt_fatal("rt_wait: thread 0x%x is already queued in a wait", t->id);

    ws->n = n;
    ws->wait_all = wait_all;
    ws->alertable = alertable;
    for (unsigned i = 0; i < n; i++) ws->blocks[i].obj = objs[i];

    /* 1. The objects first: a wait that can be satisfied is, whatever else is pending. */
    DWORD result;
    if (try_satisfy(ws, &result)) {
        rt_dispatcher_unlock();
        return result;
    }
    /* 2. Rule (SleepEx, the Ex waits): an alertable wait returns WAIT_IO_COMPLETION when an
     * APC is queued to the thread.  One that is already queued ends the wait before it
     * sleeps, and before the zero timeout is looked at (kernel order; UNSURE, see the top). */
    if (alertable && ws->apc_head) {
        rt_dispatcher_unlock();
        run_apcs(ctx, ws);
        return WAIT_IO_COMPLETION;
    }
    /* 3. Rule (dwMilliseconds): "If dwMilliseconds is zero, the function does not enter a wait
     * state if the object is not signaled; it always returns immediately."  For a sleep,
     * rule (Sleep): "A value of zero causes the thread to relinquish the remainder of its
     * time slice to any other thread that is ready to run." */
    if (timeout_ms == 0) {
        rt_dispatcher_unlock();
        if (n == 0) sched_yield();
        return WAIT_TIMEOUT;
    }

    /* 4. Queue on every object and sleep.
     * Rule (dwMilliseconds): INFINITE means the wait has no timeout; any other value is the
     * interval in milliseconds, 0xFFFFFFFE (49.7 days) included.  The deadline is on the
     * runtime's monotonic clock, in its 100 ns units, and is fixed here, as the thread joins
     * the queues: whoever sees it queued knows that its time runs from before that moment. */
    const bool timed = timeout_ms != INFINITE;
    const uint64_t began = rt_time_mono_100ns();
    const uint64_t deadline = timed ? began + (uint64_t)timeout_ms * 10000u : 0;
    ws->timeout_ms = timeout_ms;                /* these three: for the wait report only */
    ws->since = began;
    ws->since_seq = signal_seq;
    for (unsigned i = 0; i < n; i++) queue_append(&ws->blocks[i]);
    ws->waiting = true;
    __atomic_store_n(&ws->wake, 0, __ATOMIC_SEQ_CST);
    rt_dispatcher_unlock();

    for (;;) {
        if (__atomic_load_n(&ws->wake, __ATOMIC_SEQ_CST)) break;       /* satisfied or alerted: result is set */
        uint64_t ns = 0;
        if (timed) {
            uint64_t now = rt_time_mono_100ns();
            if (now >= deadline) {
                /* For the wait report: how late timed waits end (three relaxed atomic operations). */
                const uint64_t late = now - deadline;
                __atomic_fetch_add(&late_count, 1, __ATOMIC_RELAXED);
                __atomic_fetch_add(&late_total, late, __ATOMIC_RELAXED);
                if (late > __atomic_load_n(&late_max, __ATOMIC_RELAXED)) __atomic_store_n(&late_max, late, __ATOMIC_RELAXED);
                /* Time is up.  Whether the wait timed out is decided under the lock: if a
                 * signaller ended it first, it took from the object for this thread and the
                 * wait must report that. */
                rt_dispatcher_lock();
                if (ws->waiting) {
                    dequeue_all(ws);
                    ws->result = WAIT_TIMEOUT;
                }
                result = ws->result;
                rt_dispatcher_unlock();
                goto done;
            }
            ns = (deadline - now) * 100u;       /* at most 2^32 ms: no overflow */
        }
        sleep_on_word(ws, ns);
    }
    result = ws->result;
done:
    if (result == WAIT_IO_COMPLETION) run_apcs(ctx, ws);
    return result;
}

DWORD rt_wait(Ctx *ctx, unsigned n, RtObject *const *objs, bool wait_all, DWORD timeout_ms, bool alertable) {
    if (!ctx || !RT_THREAD(ctx)) rt_fatal("rt_wait: no ctx, or a ctx that belongs to no thread");

    /* Rule (WaitForMultipleObjects, nCount): "The maximum number of object handles is
     * MAXIMUM_WAIT_OBJECTS.  This parameter cannot be zero."  kernel: STATUS_INVALID_PARAMETER_1
     * for both, which is ERROR_INVALID_PARAMETER.  (A sleep, which waits on nothing, is
     * rt_sleep_ex, not a wait with n == 0.) */
    if (n == 0 || n > MAXIMUM_WAIT_OBJECTS) {
        rt_set_last_error(ctx, ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    if (!objs) rt_fatal("rt_wait: NULL object array with n = %u", n);

    /* Rule (rt/handle.h): an object whose kind has no is_signaled operation is not waitable,
     * and waiting on it fails with ERROR_INVALID_HANDLE.  UNSURE whether that is always
     * Windows's code: the kernel reports a type that cannot be waited on as
     * STATUS_OBJECT_TYPE_MISMATCH (ERROR_INVALID_HANDLE), but a handle that merely lacks the
     * SYNCHRONIZE right, which is how registry keys and file mappings are refused, as
     * ERROR_ACCESS_DENIED.  Every object is checked before anything is taken or queued. */
    for (unsigned i = 0; i < n; i++) {
        if (!objs[i]) rt_fatal("rt_wait: object %u of %u is NULL", i, n);
        if (!objs[i]->ops->is_signaled) {
            rt_set_last_error(ctx, ERROR_INVALID_HANDLE);
            return WAIT_FAILED;
        }
    }

    /* Rule (lpHandles): "The array can contain handles to objects of different types.  It may
     * not contain multiple copies of the same handle if bWaitAll is TRUE."  kernel: after it
     * has resolved the handles it compares the OBJECTS, so two handles to one object are
     * refused too: STATUS_INVALID_PARAMETER_MIX, which is ERROR_INVALID_PARAMETER.  A wait-any
     * may name an object more than once; the lowest index is reported. */
    if (wait_all)
        for (unsigned i = 0; i < n; i++)
            for (unsigned j = i + 1; j < n; j++)
                if (objs[i] == objs[j]) {
                    rt_set_last_error(ctx, ERROR_INVALID_PARAMETER);
                    return WAIT_FAILED;
                }

    rt_dispatcher_lock();
    return rt_wait_locked(ctx, n, objs, wait_all, timeout_ms, alertable);
}

DWORD rt_sleep_ex(Ctx *ctx, DWORD timeout_ms, BOOL alertable) {
    rt_dispatcher_lock();
    DWORD r = rt_wait_locked(ctx, 0, NULL, false, timeout_ms, alertable != 0);
    /* Rule (SleepEx, return value): "The return value is zero if the specified time interval
     * expired", WAIT_IO_COMPLETION if it returned because of an APC. */
    return r == WAIT_TIMEOUT ? 0 : r;
}

/* ------------------------------------------------------------------------------------------ */
/* THE WAIT REPORT (rt/sync.h, rt_wait_report_thread)                                         */
/* ------------------------------------------------------------------------------------------ */
/* Diagnostics for a run that does not end: what a thread's wait record says.  May be called
 * from a signal handler: it allocates nothing, formats no floating-point number (that can
 * allocate), writes into the caller's buffer, and takes the dispatcher lock only by trying
 * (rt_wait_report_lock: a signal may have interrupted this very thread, or any thread, inside
 * the lock): up to 200 tries a millisecond apart, then the report reads without the lock and
 * says so.  What it costs a run that never asks for it: the three stores of rt_signal,
 * the three stores at the start of a wait that sleeps, and one read of the clock at the
 * start of a wait without a timeout (a timed wait read it already). */

static size_t wr_appendf(char *buf, size_t n, size_t at, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static size_t wr_appendf(char *buf, size_t n, size_t at, const char *fmt, ...) {
    if (!n || at >= n - 1) return at;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + at, n - at, fmt, ap);
    va_end(ap);
    if (w < 0) return at;
    return at + (size_t)w < n ? at + (size_t)w : n - 1;
}

/* The object's name as ASCII, for one line of the report ("" if it has none). */
static const char *wr_name(const RtObject *o, char *out, size_t cap) {
    size_t k = 0;
    if (o->name && cap > 4) {
        out[k++] = ' ';
        out[k++] = '"';
        for (const WCHAR *p = o->name; *p && k < cap - 2; p++) out[k++] = *p >= 0x20 && *p < 0x7f ? (char)*p : '?';
        out[k++] = '"';
    }
    out[k] = 0;
    return out;
}

static bool report_locked;              /* rt_wait_report_lock took the lock and rt_wait_report_unlock has not released it */

bool rt_wait_report_lock(void) {
    if (disp_held || report_locked) return false;
    for (unsigned i = 0; i < 200; i++) {
        if (os_unfair_lock_trylock(&disp_lock)) return report_locked = true;
        usleep(1000);
    }
    return false;
}

void rt_wait_report_unlock(void) {
    if (!report_locked) return;
    report_locked = false;
    os_unfair_lock_unlock(&disp_lock);
}

size_t rt_wait_report_lateness(char *buf, size_t n, size_t at) {
    const uint64_t c = __atomic_load_n(&late_count, __ATOMIC_RELAXED), sum = __atomic_load_n(&late_total, __ATOMIC_RELAXED);
    return wr_appendf(buf, n, at, "timed waits and sleeps that ran out: %llu; they ended on average %llu and at most %llu microseconds after their deadline\n",
                      (unsigned long long)c, (unsigned long long)(c ? sum / c / 10 : 0),
                      (unsigned long long)(__atomic_load_n(&late_max, __ATOMIC_RELAXED) / 10));
}

size_t rt_wait_report_thread(char *buf, size_t n, size_t at, RtThread *t) {
    if (!t) return wr_appendf(buf, n, at, "  wait core: no thread\n");
    if (disp_held)
        at = wr_appendf(buf, n, at, "  wait core: this report runs on a thread that was interrupted inside the dispatcher lock; what follows may be half changed\n");
    else if (!report_locked)
        at = wr_appendf(buf, n, at, "  wait core: read without the dispatcher lock (it could not be taken in 200 ms, or the caller did not ask for it)\n");
    const WaitState *ws = t->sub[RT_SUB_WAIT];
    if (!ws) {
        at = wr_appendf(buf, n, at, "  wait core: not waiting; the thread has never slept in a wait or Sleep of the runtime\n");
    } else if (ws == &exited_marker) {
        at = wr_appendf(buf, n, at, "  wait core: the thread is ending (its wait record is gone)\n");
    } else if (!ws->waiting) {
        at = wr_appendf(buf, n, at, "  wait core: not waiting (not queued on any object and not in Sleep)%s\n",
                        ws->apc_head ? "; APCs are queued for it" : "");
    } else {
        const uint64_t now = rt_time_mono_100ns();
        char limit[48];
        if (ws->timeout_ms == INFINITE) snprintf(limit, sizeof limit, "no timeout");
        else snprintf(limit, sizeof limit, "timeout %lu ms", (unsigned long)ws->timeout_ms);
        if (ws->n == 0)
            at = wr_appendf(buf, n, at, "  wait core: IN Sleep for %llu ms so far (%s%s); it began after signal #%llu of the process\n",
                            (unsigned long long)((now - ws->since) / 10000u), limit, ws->alertable ? ", alertable" : "",
                            (unsigned long long)ws->since_seq);
        else
            at = wr_appendf(buf, n, at, "  wait core: WAITING for %llu ms so far for %s %u object(s) (%s%s); the wait began after signal #%llu of the process\n",
                            (unsigned long long)((now - ws->since) / 10000u), ws->wait_all ? "all of" : ws->n == 1 ? "its" : "any of",
                            ws->n, limit, ws->alertable ? ", alertable" : "", (unsigned long long)ws->since_seq);
        for (unsigned i = 0; i < ws->n; i++) {
            RtObject *o = ws->blocks[i].obj;
            char name[80];
            unsigned queued = 0;
            for (const WaitBlock *b = o->waiters; b && queued < 1000; b = b->next) queued++;
            at = wr_appendf(buf, n, at, "    [%u] %s %p%s: %s for this thread now; %u thread(s) queued on it; ", i,
                            o->ops && o->ops->name ? o->ops->name : "object", (void *)o, wr_name(o, name, sizeof name),
                            o->ops && o->ops->is_signaled && o->ops->is_signaled(o, t) ? "SIGNALLED" : "not signalled", queued);
            if (!o->sig_count)
                at = wr_appendf(buf, n, at, "never signalled\n");
            else
                at = wr_appendf(buf, n, at, "signalled %u time(s), last by thread 0x%x as signal #%llu, %s this wait began\n",
                                o->sig_count, o->sig_thread, (unsigned long long)o->sig_seq,
                                o->sig_seq > ws->since_seq ? "AFTER" : "before");
        }
    }
    return at;
}

/* ------------------------------------------------------------------------------------------ */
/* Thread exit                                                                                */
/* ------------------------------------------------------------------------------------------ */

void rt_wait_thread_exit(RtThread *t) {
    if (!t) rt_fatal("rt_wait_thread_exit: NULL thread");
    rt_dispatcher_lock();
    WaitState *ws = t->sub[RT_SUB_WAIT];
    if (ws == &exited_marker) rt_fatal("rt_wait_thread_exit: called twice for thread 0x%x", t->id);
    if (ws && ws->waiting)
        rt_fatal("rt_wait_thread_exit: thread 0x%x is inside a wait.  Ending a thread that is waiting "
                 "(TerminateThread) is not supported", t->id);
    /* Rule (CreateMutex, remarks; WaitForSingleObject, WAIT_ABANDONED): a mutex that its owner
     * did not release before it terminated is abandoned.  A thread can own a mutex without
     * ever having waited (CreateMutex with bInitialOwner), so this does not depend on `ws`. */
    unsigned abandoned = rt_sync_abandon_owned(t);
    Apc *apcs = ws ? ws->apc_head : NULL;
    t->sub[RT_SUB_WAIT] = &exited_marker;
    rt_dispatcher_unlock();

    /* APCs that were never delivered are dropped: the thread will not wait again. */
    unsigned dropped = 0;
    while (apcs) {
        Apc *next = apcs->next;
        free(apcs);
        apcs = next;
        dropped++;
    }
    free(ws);
    if (abandoned)
        rt_log(RT_CH_THREAD, RT_LL_WARN, "thread 0x%x exited owning %u mutex%s: abandoned", t->id, abandoned,
               abandoned == 1 ? "" : "es");
    if (dropped)
        rt_log(RT_CH_THREAD, RT_LL_INFO, "thread 0x%x exited with %u APC%s still queued: dropped", t->id, dropped,
               dropped == 1 ? "" : "s");
}
