/* Events, mutexes and semaphores, and the wait calls on handles (docs/RUNTIME.md section 4.2).
 * Interface and specification: rt/sync.h.  The wait core they stand on: core/wait.c.
 *
 * Each kind is an object in the handle table whose state is guarded by the dispatcher lock,
 * with the two operations the wait core asks of a waitable kind (rt/handle.h):
 *
 *   kind        is_signaled(obj, thread)             consume(obj, thread)
 *   event       set                                  auto-reset: reset.  manual-reset: nothing
 *   mutex       unowned, or owned by `thread`        `thread` becomes the owner, or its depth + 1
 *   semaphore   count > 0                            count - 1
 *
 * An operation that can make an object signalled (SetEvent, ReleaseMutex, ReleaseSemaphore,
 * abandonment) changes the state and calls rt_signal in one critical section; the wait core
 * then hands the object to the queued threads, calling consume for each.  Nothing here ever
 * blocks or wakes a thread itself.
 *
 * CREATION AND NAMES.  A named object can be found through the handle unit's name space the
 * moment rt_object_new returns it, before its fields are filled in (core/handle.c says so and
 * leaves the cure to the kind).  So every creation function calls rt_object_new and
 * initialises the object inside ONE critical section of the dispatcher lock, and every
 * function that reads or changes an object's state takes that lock: whoever finds the object
 * by name cannot look at its state before the creator has finished.  The handle unit's own
 * locks are leaf locks, so calling it under the dispatcher lock is allowed.
 *
 * LAST ERROR.  Stored through the caller's ctx (the thread block, where the guest reads it).
 * The handle unit reports its own failures through rt_thread_set_last_error, which is the
 * same place on a guest thread; where one of its calls has exactly one documented failure
 * code this file stores that code through ctx itself, and where it has several it copies the
 * value across.  (The copy matters only on the hand-made threads of the tests, which have a
 * ctx but are not the runtime's guest threads.)
 *
 * WHERE THE RULES COME FROM.  "Rule:" is the Microsoft documentation of the function named.
 * "kernel:" is what the NT kernel does where the documentation is silent (which status, in
 * which order), as far as it could be established without a Windows machine: from the
 * ReactOS reimplementation of the Nt and Ke routines and Wine's kernelbase, and the NTSTATUS
 * to Win32 error table.  What could not be established is marked UNSURE.
 */
#include <stdio.h>
#include <string.h>

#include "core.h"
#include "rt/handle.h"
#include "rt/log.h"
#include "rt/sync.h"

typedef struct RtEvent {
    RtObject obj;
    bool     manual;            /* manual-reset: a satisfied wait leaves it set */
    bool     set;
} RtEvent;

typedef struct RtMutex {
    RtObject        obj;
    RtThread       *owner;      /* NULL: unowned */
    uint32_t        depth;      /* waits the owner has not yet matched with a release; 0 iff unowned */
    bool            abandoned;  /* unowned because the owner exited; the next wait that takes it reports it */
    struct RtMutex *own_next, *own_prev;        /* the list of all owned mutexes (below) */
} RtMutex;

typedef struct RtSemaphore {
    RtObject obj;
    LONG     count;             /* 0 .. max */
    LONG     max;
} RtSemaphore;

/* Every mutex that has an owner, in no particular order (dispatcher lock).  It exists for one
 * purpose: finding the mutexes a thread owns when it exits.  One list for the process rather
 * than one per thread, because thread exits are rare and owned kernel mutexes few; the scan
 * at exit is over all of them. */
static RtMutex *owned_mutexes;

/* ------------------------------------------------------------------------------------------ */
/* Helpers                                                                                    */
/* ------------------------------------------------------------------------------------------ */

static RtThread *thread_of(Ctx *ctx, const char *fn) {
    if (!ctx || !RT_THREAD(ctx)) rt_fatal("%s: no ctx, or a ctx that belongs to no thread", fn);
    return RT_THREAD(ctx);
}

/* The object behind `h` if it is of `kind`, with a reference.  NULL with last error
 * ERROR_INVALID_HANDLE otherwise: rt_handle_ref's one failure code (rt/handle.h). */
static RtObject *ref_kind(Ctx *ctx, HANDLE h, RtKind kind) {
    RtObject *o = rt_handle_ref(h, kind);
    if (!o) rt_set_last_error(ctx, ERROR_INVALID_HANDLE);
    return o;
}

/* The end of every Create function: turn the object into a handle and set last error. */
static HANDLE created(Ctx *ctx, RtObject *obj, bool existed) {
    if (!obj) {
        /* rt_object_new failed: ERROR_INVALID_HANDLE (the name belongs to another kind) or
         * ERROR_NOT_ENOUGH_MEMORY.  Rule (CreateEvent, CreateMutex, CreateSemaphore): "If
         * lpName matches the name of an existing semaphore, mutex, waitable timer, job, or
         * file-mapping object, the function fails and the GetLastError function returns
         * ERROR_INVALID_HANDLE." */
        rt_set_last_error(ctx, rt_thread_get_last_error());
        return NULL;
    }
    HANDLE h = rt_handle_new(obj);              /* consumes the reference, also when it fails */
    if (!h) {
        rt_set_last_error(ctx, ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    /* Rule: "If the named event object existed before the function call, the function returns
     * a handle to the existing object and GetLastError returns ERROR_ALREADY_EXISTS" (the same
     * sentence for mutexes and semaphores; RUNTIME.md 3.4 lists it as the exception to "leave
     * last error alone on success").
     * kernel32: on a fresh creation the function stores ERROR_SUCCESS (it converts the kernel's
     * status in both cases), which is what lets a caller test for ERROR_ALREADY_EXISTS after a
     * success without clearing last error first.  From Wine's kernelbase; UNSURE only in that
     * it was not seen on Windows. */
    rt_set_last_error(ctx, existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return h;
}

/* The whole of every Open function.  rt_object_open fails with ERROR_FILE_NOT_FOUND,
 * ERROR_INVALID_HANDLE (the name belongs to another kind) or ERROR_INVALID_PARAMETER (NULL). */
static HANDLE opened(Ctx *ctx, RtKind kind, const WCHAR *name, const char *fn) {
    thread_of(ctx, fn);
    RtObject *obj = rt_object_open(kind, name);
    if (!obj) {
        rt_set_last_error(ctx, rt_thread_get_last_error());
        return NULL;
    }
    HANDLE h = rt_handle_new(obj);
    if (!h) rt_set_last_error(ctx, ERROR_NOT_ENOUGH_MEMORY);
    return h;                                   /* success leaves last error alone */
}

/* ------------------------------------------------------------------------------------------ */
/* Events                                                                                     */
/* ------------------------------------------------------------------------------------------ */

static bool event_is_signaled(RtObject *o, RtThread *t) {
    (void)t;
    return ((RtEvent *)o)->set;
}

/* Rule (CreateEvent, bManualReset): for an auto-reset event "the system automatically resets
 * the event state to nonsignaled after a single waiting thread has been released"; a
 * manual-reset event "requires the use of the ResetEvent function". */
static void event_consume(RtObject *o, RtThread *t) {
    (void)t;
    RtEvent *e = (RtEvent *)o;
    if (!e->manual) e->set = false;
}

static const RtObjectOps event_ops = { .name = "event", .is_signaled = event_is_signaled, .consume = event_consume };

HANDLE rt_event_create(Ctx *ctx, BOOL manual_reset, BOOL initial_state, const WCHAR *name) {
    thread_of(ctx, "rt_event_create");
    bool existed = false;
    rt_dispatcher_lock();
    RtEvent *e = (RtEvent *)rt_object_new(RT_K_EVENT, sizeof *e, &event_ops, name, &existed);
    /* Rule: when the name exists, "the bManualReset and bInitialState parameters are ignored
     * because they have already been set by the creating process". */
    if (e && !existed) {
        e->manual = manual_reset != 0;
        e->set = initial_state != 0;
    }
    rt_dispatcher_unlock();
    return created(ctx, e ? &e->obj : NULL, existed);
}

HANDLE rt_event_open(Ctx *ctx, const WCHAR *name) { return opened(ctx, RT_K_EVENT, name, "rt_event_open"); }

static RtEvent *checked_event(RtObject *o, const char *fn) {
    if (!o) rt_fatal("%s: NULL object", fn);
    if (o->kind != RT_K_EVENT) rt_fatal("%s: object %p is a %s, not an event", fn, (void *)o, o->ops->name);
    return (RtEvent *)o;
}

/* Dispatcher lock held.  Rule (SetEvent): a manual-reset event "remains signaled until it is
 * set explicitly to the nonsignaled state by the ResetEvent function.  Any number of waiting
 * threads ... can be released"; an auto-reset event "remains signaled until a single waiting
 * thread is released, at which time the system automatically sets the state to nonsignaled.
 * If no threads are waiting, the event object's state remains signaled."  rt_signal does
 * both: it satisfies queued waits in order and event_consume resets an auto-reset event when
 * the first one is satisfied. */
static void event_set_locked(RtEvent *e) {
    e->set = true;
    rt_signal(&e->obj);
}

void rt_event_set_object(RtObject *event) {
    RtEvent *e = checked_event(event, "rt_event_set_object");
    rt_dispatcher_lock();
    event_set_locked(e);
    rt_dispatcher_unlock();
}

void rt_event_reset_object(RtObject *event) {
    RtEvent *e = checked_event(event, "rt_event_reset_object");
    rt_dispatcher_lock();
    e->set = false;                             /* nothing becomes signalled: nobody to wake */
    rt_dispatcher_unlock();
}

BOOL rt_event_set(Ctx *ctx, HANDLE event) {
    thread_of(ctx, "rt_event_set");
    RtObject *o = ref_kind(ctx, event, RT_K_EVENT);
    if (!o) return FALSE;
    rt_event_set_object(o);
    rt_object_unref(o);
    return TRUE;
}

BOOL rt_event_reset(Ctx *ctx, HANDLE event) {
    thread_of(ctx, "rt_event_reset");
    RtObject *o = ref_kind(ctx, event, RT_K_EVENT);
    if (!o) return FALSE;
    rt_event_reset_object(o);
    rt_object_unref(o);
    return TRUE;
}

BOOL rt_event_pulse(Ctx *ctx, HANDLE event) {
    thread_of(ctx, "rt_event_pulse");
    RtObject *o = ref_kind(ctx, event, RT_K_EVENT);
    if (!o) return FALSE;
    RtEvent *e = (RtEvent *)o;
    /* Rule (PulseEvent): "For a manual-reset event object, all waiting threads that can be
     * released immediately are released.  The function then resets the event object's state to
     * nonsignaled and returns.  For an auto-reset event object, the function releases a single
     * waiting thread and then resets the event object's state to nonsignaled, even if multiple
     * threads are waiting.  If no threads are waiting, or if no thread can be released
     * immediately, PulseEvent simply sets the event object's state to nonsignaled and returns."
     * Implemented by that rule, not trapped: set, release, reset, in one critical section. */
    rt_dispatcher_lock();
    event_set_locked(e);
    e->set = false;
    rt_dispatcher_unlock();
    rt_object_unref(o);
    return TRUE;
}

/* ------------------------------------------------------------------------------------------ */
/* Mutexes                                                                                    */
/* ------------------------------------------------------------------------------------------ */

/* Dispatcher lock held. */
static void owned_link(RtMutex *m) {
    m->own_prev = NULL;
    m->own_next = owned_mutexes;
    if (owned_mutexes) owned_mutexes->own_prev = m;
    owned_mutexes = m;
}

/* Dispatcher lock held. */
static void owned_unlink(RtMutex *m) {
    if (m->own_prev) m->own_prev->own_next = m->own_next;
    else owned_mutexes = m->own_next;
    if (m->own_next) m->own_next->own_prev = m->own_prev;
    m->own_next = m->own_prev = NULL;
}

/* Rule (CreateMutex, remarks): "The state of a mutex object is signaled when it is not owned
 * by any thread", and "The thread that owns a mutex can specify the same mutex in repeated
 * wait function calls without blocking its execution." */
static bool mutex_is_signaled(RtObject *o, RtThread *t) {
    RtMutex *m = (RtMutex *)o;
    return !m->owner || m->owner == t;
}

static void mutex_consume(RtObject *o, RtThread *t) {
    RtMutex *m = (RtMutex *)o;
    if (m->owner == t) {
        /* kernel: the depth is a signed 32-bit count (it runs from 1 when free down to the
         * most negative value), and the acquisition after about 2^31 raises the exception
         * STATUS_MUTANT_LIMIT_EXCEEDED.  Exceptions are not delivered here (RUNTIME.md 9.3), so
         * the run stops instead, at 2^31 - 1 acquisitions: two short of where Windows raises. */
        if (m->depth >= (uint32_t)INT32_MAX)
            rt_unimplemented(&t->ctx, __FILE__, __LINE__, "a mutex acquired recursively %u times by thread 0x%x: one "
                             "more raises STATUS_MUTANT_LIMIT_EXCEEDED on Windows", m->depth, t->id);
        m->depth++;
        return;
    }
    m->owner = t;
    m->depth = 1;
    owned_link(m);
    /* Rule (WaitForSingleObject, WAIT_ABANDONED): "The specified object is a mutex object that
     * was not released by the thread that owned the mutex object before the owning thread
     * terminated.  Ownership of the mutex object is granted to the calling thread and the
     * mutex state is set to nonsignaled."  kernel: the abandoned mark is cleared by the wait
     * that reports it, so only that one wait returns WAIT_ABANDONED. */
    if (m->abandoned) {
        m->abandoned = false;
        rt_wait_report_abandoned();
    }
}

/* The last reference is gone.  A mutex can die while it is owned (its last handle closed by
 * anyone; on Windows the object goes away the same way): it must leave the list of owned
 * mutexes, which the owner's exit would otherwise walk into freed memory.  The dispatcher
 * lock is taken even to look at `owner`, because that exit may be running now. */
static void mutex_destroy(RtObject *o) {
    RtMutex *m = (RtMutex *)o;
    bool held = rt_dispatcher_held();           /* rt_object_unref was called under the lock */
    if (!held) rt_dispatcher_lock();
    if (m->owner) owned_unlink(m);
    m->owner = NULL;
    if (!held) rt_dispatcher_unlock();
}

static const RtObjectOps mutex_ops = {
    .name = "mutex", .destroy = mutex_destroy, .is_signaled = mutex_is_signaled, .consume = mutex_consume,
};

HANDLE rt_mutex_create(Ctx *ctx, BOOL initial_owner, const WCHAR *name) {
    RtThread *t = thread_of(ctx, "rt_mutex_create");
    bool existed = false;
    rt_dispatcher_lock();
    RtMutex *m = (RtMutex *)rt_object_new(RT_K_MUTEX, sizeof *m, &mutex_ops, name, &existed);
    /* Rule (bInitialOwner): "If this value is TRUE and the caller created the mutex, the
     * calling thread obtains initial ownership of the mutex object.  Otherwise, the calling
     * thread does not obtain ownership of the mutex."  So it is ignored when the name exists. */
    if (m && !existed && initial_owner) {
        m->owner = t;
        m->depth = 1;
        owned_link(m);
    }
    rt_dispatcher_unlock();
    return created(ctx, m ? &m->obj : NULL, existed);
}

HANDLE rt_mutex_open(Ctx *ctx, const WCHAR *name) { return opened(ctx, RT_K_MUTEX, name, "rt_mutex_open"); }

/* Dispatcher lock held.  0, or the Win32 error. */
static DWORD mutex_release_locked(RtMutex *m, RtThread *t) {
    /* Rule (ReleaseMutex): "The ReleaseMutex function fails if the calling thread does not own
     * the mutex object."  kernel: STATUS_MUTANT_NOT_OWNED, which is ERROR_NOT_OWNER; also when
     * nobody owns it.
     * UNSURE about one case: a mutex that is unowned BECAUSE IT WAS ABANDONED.  As remembered
     * from the kernel's release routine, it then raises STATUS_ABANDONED instead, which is a
     * success code, so ReleaseMutex may return TRUE there (and change nothing).  The
     * documented rule is followed here: FALSE and ERROR_NOT_OWNER. */
    if (m->owner != t) return ERROR_NOT_OWNER;
    /* Rule: "to release its ownership, the thread must call ReleaseMutex one time for each
     * time that it obtained ownership". */
    if (--m->depth == 0) {
        m->owner = NULL;
        owned_unlink(m);
        rt_signal(&m->obj);
    }
    return 0;
}

BOOL rt_mutex_release(Ctx *ctx, HANDLE mutex) {
    RtThread *t = thread_of(ctx, "rt_mutex_release");
    RtObject *o = ref_kind(ctx, mutex, RT_K_MUTEX);
    if (!o) return FALSE;
    rt_dispatcher_lock();
    DWORD err = mutex_release_locked((RtMutex *)o, t);
    rt_dispatcher_unlock();
    rt_object_unref(o);
    if (err) {
        rt_set_last_error(ctx, err);
        return FALSE;
    }
    return TRUE;
}

unsigned rt_sync_abandon_owned(RtThread *t) {
    if (!rt_dispatcher_held()) rt_fatal("rt_sync_abandon_owned: called without the dispatcher lock");
    unsigned n = 0;
    for (RtMutex *m = owned_mutexes, *next; m; m = next) {
        next = m->own_next;
        if (m->owner != t) continue;
        /* Whatever the depth, the mutex is released in full and marked.  rt_signal hands it to
         * the first queued thread, whose wait returns WAIT_ABANDONED_0 + index.  It touches
         * the list only through mutex_consume, which links at the head: `next` stays valid. */
        m->owner = NULL;
        m->depth = 0;
        m->abandoned = true;
        owned_unlink(m);
        rt_signal(&m->obj);
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------------------------------ */
/* Semaphores                                                                                 */
/* ------------------------------------------------------------------------------------------ */

/* Rule (CreateSemaphore, remarks): "The state of a semaphore object is signaled when its count
 * is greater than zero, and nonsignaled when its count is equal to zero", and "Each time a
 * waiting thread is released because of the semaphore's signaled state, the count of the
 * semaphore is decreased by one." */
static bool semaphore_is_signaled(RtObject *o, RtThread *t) {
    (void)t;
    return ((RtSemaphore *)o)->count > 0;
}

static void semaphore_consume(RtObject *o, RtThread *t) {
    (void)t;
    RtSemaphore *s = (RtSemaphore *)o;
    if (s->count <= 0) rt_fatal("semaphore %p consumed with count %d", (void *)o, s->count);
    s->count--;
}

static const RtObjectOps semaphore_ops = {
    .name = "semaphore", .is_signaled = semaphore_is_signaled, .consume = semaphore_consume,
};

HANDLE rt_semaphore_create(Ctx *ctx, LONG initial_count, LONG maximum_count, const WCHAR *name) {
    thread_of(ctx, "rt_semaphore_create");
    /* Rule (lInitialCount): "This value must be greater than or equal to zero and less than or
     * equal to lMaximumCount."  Rule (lMaximumCount): "This value must be greater than zero."
     * kernel: checked before the name is looked at, STATUS_INVALID_PARAMETER, so bad counts
     * fail even when a semaphore of that name exists. */
    if (maximum_count <= 0 || initial_count < 0 || initial_count > maximum_count) {
        rt_set_last_error(ctx, ERROR_INVALID_PARAMETER);
        return NULL;
    }
    bool existed = false;
    rt_dispatcher_lock();
    RtSemaphore *s = (RtSemaphore *)rt_object_new(RT_K_SEMAPHORE, sizeof *s, &semaphore_ops, name, &existed);
    if (s && !existed) {                        /* the counts of an existing semaphore are not changed */
        s->count = initial_count;
        s->max = maximum_count;
    }
    rt_dispatcher_unlock();
    return created(ctx, s ? &s->obj : NULL, existed);
}

HANDLE rt_semaphore_open(Ctx *ctx, const WCHAR *name) {
    return opened(ctx, RT_K_SEMAPHORE, name, "rt_semaphore_open");
}

/* Dispatcher lock held.  0 with *previous set, or the Win32 error with nothing changed. */
static DWORD semaphore_release_locked(RtSemaphore *s, LONG release_count, LONG *previous) {
    /* Rule (lReleaseCount): "The value must be greater than zero."  kernel:
     * STATUS_INVALID_PARAMETER. */
    if (release_count <= 0) return ERROR_INVALID_PARAMETER;
    /* Rule: "If the specified amount would cause the semaphore's count to exceed the maximum
     * count that was specified when the semaphore was created, the count is not changed and
     * the function returns FALSE."  kernel: STATUS_SEMAPHORE_LIMIT_EXCEEDED, which is
     * ERROR_TOO_MANY_POSTS.  The sum is formed in 64 bits: count + release can pass 2^31. */
    if ((int64_t)s->count + (int64_t)release_count > (int64_t)s->max) return ERROR_TOO_MANY_POSTS;
    *previous = s->count;
    s->count += release_count;
    rt_signal(&s->obj);                         /* each thread released takes one unit back */
    return 0;
}

BOOL rt_semaphore_release(Ctx *ctx, HANDLE semaphore, LONG release_count, LONG *previous_count) {
    thread_of(ctx, "rt_semaphore_release");
    RtObject *o = ref_kind(ctx, semaphore, RT_K_SEMAPHORE);
    if (!o) return FALSE;
    LONG previous = 0;
    rt_dispatcher_lock();
    DWORD err = semaphore_release_locked((RtSemaphore *)o, release_count, &previous);
    rt_dispatcher_unlock();
    rt_object_unref(o);
    if (err) {
        rt_set_last_error(ctx, err);
        return FALSE;
    }
    /* Rule (lpPreviousCount): "A pointer to a variable to receive the previous count for the
     * semaphore.  This parameter can be NULL if the previous count is not required."  Guest
     * memory: written outside the lock. */
    if (previous_count) *previous_count = previous;
    return TRUE;
}

/* ------------------------------------------------------------------------------------------ */
/* Waiting on handles                                                                         */
/* ------------------------------------------------------------------------------------------ */

DWORD rt_wait_handles(Ctx *ctx, DWORD count, const HANDLE *handles, BOOL wait_all, DWORD timeout_ms, BOOL alertable) {
    thread_of(ctx, "rt_wait_handles");
    /* Rule (nCount): "The maximum number of object handles is MAXIMUM_WAIT_OBJECTS.  This
     * parameter cannot be zero."  Checked before any handle is looked at (rt_wait checks it
     * too, for callers that come with objects). */
    if (count == 0 || count > MAXIMUM_WAIT_OBJECTS) {
        rt_set_last_error(ctx, ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    if (!handles)
        rt_unimplemented(ctx, __FILE__, __LINE__, "a wait on %u handles with a NULL handle array (Windows raises an "
                         "access violation)", count);

    /* A reference to each object for the length of the wait (rt/handle.h): closing a handle
     * while a thread waits on it does not free the object under the waiter.  Rule: "If one of
     * these handles is closed while the wait is still pending, the function's behavior is
     * undefined"; here the wait simply goes on.
     * kernel: the handles are resolved in order and the first bad one fails the call with
     * STATUS_INVALID_HANDLE, before the objects are compared with each other. */
    RtObject *objs[MAXIMUM_WAIT_OBJECTS];
    if (count > 1 && RT_LOG_ON(RT_CH_TRACE, RT_LL_DEBUG)) {
        /* The shim trace prints the address of the handle array, which is on the caller's
         * stack; this line has the handles, in the order that decides a wait-any. */
        char buf[16 + 9 * 16];
        size_t at = 0;
        for (DWORD i = 0; i < count && i < 16 && at < sizeof buf; i++)
            at += (size_t)snprintf(buf + at, sizeof buf - at, "%s%08llx", i ? "," : "", (unsigned long long)(uintptr_t)handles[i]);
        rt_log(RT_CH_TRACE, RT_LL_DEBUG, "%04x:Wait %s of %u handles [%s%s] timeout=%08x", thread_of(ctx, "rt_wait_handles")->id,
               wait_all ? "all" : "any", (unsigned)count, buf, count > 16 ? ",..." : "", (unsigned)timeout_ms);
    }
    for (DWORD i = 0; i < count; i++) {
        objs[i] = ref_kind(ctx, handles[i], RT_K_ANY);
        if (!objs[i]) {
            while (i > 0) rt_object_unref(objs[--i]);
            return WAIT_FAILED;
        }
    }
    DWORD r = rt_wait(ctx, count, objs, wait_all != 0, timeout_ms, alertable != 0);
    for (DWORD i = 0; i < count; i++) rt_object_unref(objs[i]);
    return r;
}

DWORD rt_wait_handle(Ctx *ctx, HANDLE handle, DWORD timeout_ms, BOOL alertable) {
    return rt_wait_handles(ctx, 1, &handle, FALSE, timeout_ms, alertable);
}

DWORD rt_signal_and_wait(Ctx *ctx, HANDLE to_signal, HANDLE to_wait, DWORD timeout_ms, BOOL alertable) {
    RtThread *t = thread_of(ctx, "rt_signal_and_wait");
    /* kernel (NtSignalAndWaitForSingleObject): both handles are resolved, and the object to
     * wait on is checked, before anything is signalled; then the signal is made according to
     * the type of the first object and a failure there (not the owner, semaphore full, a type
     * that cannot be signalled) ends the call without waiting. */
    RtObject *sig = ref_kind(ctx, to_signal, RT_K_ANY);
    if (!sig) return WAIT_FAILED;
    RtObject *obj = ref_kind(ctx, to_wait, RT_K_ANY);
    if (!obj) {
        rt_object_unref(sig);
        return WAIT_FAILED;
    }
    DWORD err = 0;
    /* Rule (hObjectToSignal): "A handle to the object to be signaled.  This object can be a
     * semaphore, a mutex, or an event."  kernel: any other type is STATUS_OBJECT_TYPE_MISMATCH,
     * which is ERROR_INVALID_HANDLE; so is an object that cannot be waited on. */
    if (sig->kind != RT_K_EVENT && sig->kind != RT_K_MUTEX && sig->kind != RT_K_SEMAPHORE) err = ERROR_INVALID_HANDLE;
    else if (!obj->ops->is_signaled) err = ERROR_INVALID_HANDLE;

    DWORD r = WAIT_FAILED;
    if (!err) {
        /* The signal and the start of the wait are one critical section.  The documentation
         * now says the two "are not guaranteed to be performed as an atomic operation"; doing
         * them as one is the stronger behaviour and the one the function was introduced for
         * (releasing a mutex and waiting for a pulse without missing the pulse). */
        rt_dispatcher_lock();
        LONG previous;
        switch (sig->kind) {
        case RT_K_EVENT:     event_set_locked((RtEvent *)sig); break;
        case RT_K_MUTEX:     err = mutex_release_locked((RtMutex *)sig, t); break;
        default:             err = semaphore_release_locked((RtSemaphore *)sig, 1, &previous); break;
        }
        if (err) rt_dispatcher_unlock();
        else r = rt_wait_locked(ctx, 1, &obj, false, timeout_ms, alertable != 0);    /* releases the lock */
    }
    rt_object_unref(sig);
    rt_object_unref(obj);
    if (err) rt_set_last_error(ctx, err);
    return r;
}

/* ------------------------------------------------------------------------------------------ */
/* Looking at an object                                                                       */
/* ------------------------------------------------------------------------------------------ */

void rt_sync_query(RtObject *obj, RtSyncState *out) {
    if (!obj || !out) rt_fatal("rt_sync_query: NULL %s", obj ? "result" : "object");
    memset(out, 0, sizeof *out);
    out->kind = obj->kind;
    const bool held = rt_dispatcher_held();     /* usable from inside a kind's own critical section */
    if (!held) rt_dispatcher_lock();
    out->waiters = rt_wait_queue_length(obj);
    switch (obj->kind) {
    case RT_K_EVENT: {
        const RtEvent *e = (const RtEvent *)obj;
        out->manual_reset = e->manual;
        out->set = e->set;
        break;
    }
    case RT_K_MUTEX: {
        const RtMutex *m = (const RtMutex *)obj;
        out->owned = m->owner != NULL;
        out->abandoned = m->abandoned;
        out->owner_thread_id = m->owner ? m->owner->id : 0;
        out->recursion = m->depth;
        break;
    }
    case RT_K_SEMAPHORE: {
        const RtSemaphore *s = (const RtSemaphore *)obj;
        out->count = s->count;
        out->maximum = s->max;
        break;
    }
    default:
        rt_fatal("rt_sync_query: object %p is a %s, not an event, mutex or semaphore", (void *)obj, obj->ops->name);
    }
    if (!held) rt_dispatcher_unlock();
}
