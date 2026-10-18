/* The thread unit's own declarations: what core/thread.c, thread_run.c, thread_tls.c,
 * thread_fls.c, thread_ctl.c, thread_attach.c, thread_report.c and process.c share.  Nothing else includes
 * this file.  The public interface and the rules for the two locks are in rt/thread.h; read
 * its LOCKS paragraph before using anything here.
 */
#ifndef RT_THREAD_PRIV_H
#define RT_THREAD_PRIV_H

#include <pthread.h>
#include <setjmp.h>

#include "core.h"
#include "rt/handle.h"
#include "rt/sync.h"

/* ---- the two kernel objects -------------------------------------------------------------- */

/* The object behind a thread handle (RT_K_THREAD).  One per guest thread, made with the
 * thread and destroyed with the last reference; destroying it frees the RtThread.  While the
 * thread is running the thread unit holds one reference (RtThreadPriv.object).
 * `thread` and `id` never change.  Every other field is guarded by the DISPATCHER lock. */
typedef struct RtThreadObject {
    RtObject  obj;
    RtThread *thread;           /* valid for as long as this object */
    uint32_t  id;               /* == thread->id */
    uint32_t  exited;           /* 0, then 1 for ever from step 7 of rt_thread_exit: the signalled state */
    DWORD     exit_code;        /* STILL_ACTIVE until exited */
    int32_t   priority;         /* THREAD_PRIORITY_NORMAL at creation; rt_thread_set_priority */
    uint32_t  has_affinity;     /* 1 once rt_thread_set_affinity has succeeded for this thread */
    uint32_t  affinity_epoch;   /* RtProcessObject.affinity_changes at that moment */
    uint64_t  affinity;         /* the thread's own mask; meaningful only if has_affinity */
    uint32_t  ideal;            /* rt_thread_set_ideal_processor: the preferred processor, 0 at creation */
} RtThreadObject;

/* The one process object (RT_K_PROCESS), made by rt_thread_attach_main and registered with
 * rt_handle_set_process_object.  Never signalled, never destroyed.  The fields are guarded
 * by the DISPATCHER lock. */
typedef struct RtProcessObject {
    RtObject  obj;
    DWORD     priority_class;   /* NORMAL_PRIORITY_CLASS at creation */
    uint32_t  affinity_changes; /* how many times rt_process_set_affinity has succeeded */
    uint64_t  affinity;         /* the system mask at creation: bits 0 .. cpu_count-1 */
    uint64_t  creation_time;    /* rt_time_utc_100ns() when the object was made; never changes, so it
                                   is read without the lock (rt_process_times) */
} RtProcessObject;

/* The process object: a borrowed pointer, valid from rt_init on for ever.  Fatal before. */
RtProcessObject *rt_process_object(void);

/* The system affinity mask: bits 0 to rt_machine()->cpu_count - 1. */
uint64_t rt_system_affinity(void);

/* ---- per-thread state: RtThread.sub[RT_SUB_THREAD] --------------------------------------- */

typedef enum {
    RT_TK_MAIN = 1,             /* guest thread 1 */
    RT_TK_CREATED,              /* rt_thread_create: has a base frame for rt_thread_exit */
    RT_TK_ATTACHED              /* rt_thread_attach: a host thread the runtime did not start */
} RtThreadKind;

/* Allocated in one block with the RtThread (static for guest thread 1), so it lives exactly
 * as long as the RtThread: until the thread object is destroyed. */
typedef struct RtThreadPriv {
    RtThreadObject *object;     /* the unit's reference while the thread is in the list; NULL after
                                   step 8.  Read by the thread itself, or under the THREAD lock for a
                                   thread found in the list (rt_object_ref is then safe) */
    uint32_t  kind;             /* RtThreadKind; never changes */
    uint32_t  finishing;        /* 1 from the start of rt_thread_finish; own thread only */

    /* core/thread_run.c.  own thread only, except the gate. */
    GUESTFN   start;            /* the start routine and its argument */
    uint64_t  param;
    jmp_buf   exit_jmp;         /* the base frame rt_thread_exit unwinds to */
    uint32_t  exit_armed;       /* 1 while exit_jmp is valid */
    DWORD     exit_code;        /* handed from rt_thread_exit to the base frame */
    pthread_mutex_t gate_lock;  /* guards suspend_count */
    pthread_cond_t  gate_cond;  /* signalled when suspend_count becomes 0 */
    uint32_t  suspend_count;    /* 1: created suspended, not yet resumed; else 0 */
    uint32_t  host_port;        /* the host thread's Mach port (pthread_mach_thread_np), set when the host thread
                                   becomes this guest thread; 0 before.  Read by core/thread_report.c only */

    /* core/thread_fls.c.  NULL, or RT_FLS_SLOTS values (calloc), made at the thread's first
     * rt_fls_set.  The owner reads and writes its own values without a lock, each access
     * with __atomic_load_n / __atomic_store_n (SEQ_CST); rt_fls_free on another thread
     * swaps a value to 0 with __atomic_exchange_n under the THREAD lock, finding the array
     * through the list.  Only the owner makes the array (it stores the pointer with
     * __atomic_store_n; other threads load it with __atomic_load_n) and only
     * rt_fls_thread_exit frees it. */
    uint64_t *fls;

    /* core/process.c, rt_thread_execution_state.  own thread only.  0: the thread has never
     * set a state (it then reads as ES_CONTINUOUS alone); otherwise the flags of its last call
     * with ES_CONTINUOUS, which have that bit and so are never 0. */
    EXECUTION_STATE exec_state;

    /* core/thread_com.c.  own thread only. */
    uint32_t  com_count;        /* initialisations not yet given back (rt_com_initialize) */
    uint32_t  com_model;        /* COINIT_MULTITHREADED or COINIT_APARTMENTTHREADED; meaningful while com_count > 0 */
    uint32_t  ole_count;        /* successful rt_ole_initialize calls; never lowered */
} RtThreadPriv;

#define RT_THREAD_PRIV(t)  ((RtThreadPriv *)(t)->sub[RT_SUB_THREAD])

/* ---- the thread lock and the list (core/thread.c) ---------------------------------------- */

/* Take and release the thread lock (rt/thread.h, LOCKS).  Not recursive: taking it twice on
 * one host thread, or releasing it unheld, crashes in os_unfair_lock. */
void rt_thread_list_lock(void);
void rt_thread_list_unlock(void);

/* First running guest thread (the newest), or NULL; the rest follow through RtThread.next.
 * THREAD lock held, for the whole walk. */
RtThread *rt_thread_list_head(void);

/* For core/thread_report.c, which may run in a signal handler: try the thread lock once
 * (true: taken, release it with rt_thread_list_unlock), and the list's head read without
 * asserting anything. */
bool rt_thread_list_trylock(void);
RtThread *rt_thread_list_head_unlocked(void);

/* ---- building blocks of creation and exit (core/thread.c) -------------------------------- */

/* A new guest thread that is not running anything yet: the RtThread and RtThreadPriv (one
 * zeroed block), a fresh id, the thread block and a guest stack of `guest_stack` bytes (a
 * multiple of 64 KB) as for guest thread 1, the Ctx ready for a first guest call, the thread
 * object (not signalled, STILL_ACTIVE, priority 0) with the unit's reference in
 * RtThreadPriv.object and its address in sub[RT_SUB_HANDLE].  The thread is put in the list.
 * Its static TLS array is zero (part of the zeroed thread block) and stays so until
 * rt_loader_thread_attach runs on the thread itself.  The gate is initialised with suspend_count 0.
 * NULL with last error ERROR_NOT_ENOUGH_MEMORY if host or guest memory runs out; nothing is
 * left behind then.  Called with no lock held, on any guest thread or on a host thread. */
RtThread *rt_thread_new(uint64_t guest_stack, RtThreadKind kind);

/* Undo rt_thread_new for a thread that never ran and never will: out of the list,
 * guest memory freed (the loader never saw the thread), sub[RT_SUB_HANDLE] cleared, the unit's
 * reference dropped (which frees `t` unless a handle still names the object). */
void rt_thread_discard(RtThread *t);

/* The calling host thread starts being guest thread `t`: rt_thread_self() returns t, the
 * host thread takes t's name if it has one, then rt_thread_cpu_modes(t) and
 * rt_crash_thread_enter(t).  Fatal if the host thread already is a guest thread. */
void rt_thread_bind(RtThread *t);

/* The one hook every guest thread passes through on its own host thread before guest code
 * (rt/thread.h, CPU MODES): the x86 memory-ordering mode by rt_config()->tso and RT_TSO,
 * then the host float mode.  Sets t->tso.  Called by rt_thread_bind and for guest thread 1. */
void rt_thread_cpu_modes(RtThread *t);

/* Steps 1 to 8 of rt_thread_exit for the calling thread `t`, which then is no guest thread
 * any more; returns to its caller, which must not touch `t` again (it may be freed).  The
 * guest rsp is left where it is, so guest frames above it stay intact for the FLS callbacks
 * and DllMain.  Fatal: t is not rt_thread_self(); t is guest thread 1; called twice. */
void rt_thread_finish(RtThread *t, DWORD code);

/* ---- core/thread_fls.c -------------------------------------------------------------------- */

/* Step 1 of rt_thread_exit, on the exiting thread, with no lock held.  If the thread has no
 * FLS array: nothing.  Otherwise, ONE pass over the indices 1 to RT_FLS_SLOTS-1 in rising
 * order: for an index whose value is nonzero, set the value to 0 and, if the index is
 * allocated and has a callback (both read under the THREAD lock, released before the call),
 * call callback(value) through rt_guest_call(ctx, callback, 1, &value).  A value that a
 * callback stores during the pass at an index already passed is not called for (Windows
 * makes one pass; not verified): it is dropped, logged at warn level on the thread channel.
 * Then the array is freed and RtThreadPriv.fls set to NULL (under the THREAD lock, so that a
 * concurrent rt_fls_free never sees a freed array). */
void rt_fls_thread_exit(Ctx *ctx);

#endif /* RT_THREAD_PRIV_H */
