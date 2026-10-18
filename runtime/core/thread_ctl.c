/* Looking at and adjusting a thread through its handle: GetExitCodeThread, SetThreadPriority,
 * GetThreadPriority, SetThreadAffinityMask, OpenThread, and the handle-to-RtThread lookup
 * (docs/RUNTIME.md section 6.3).  Interface and specification: rt/thread.h, "looking at and
 * adjusting a thread".  Every function is built here; its failures are the ones rt/thread.h lists.
 *
 * No state of its own.  Every function but rt_thread_open starts with
 * rt_handle_ref(thread, RT_K_THREAD), which resolves (HANDLE)-2 and sets ERROR_INVALID_HANDLE
 * itself, and reads or writes the RtThreadObject (thread_priv.h) under the DISPATCHER lock.
 * rt_thread_open walks rt_thread_list_head() under the THREAD lock.
 */
#include "thread_priv.h"
#include "rt/machine.h"

/* Fatal before anything else: Windows raises an access violation for a NULL code. */
BOOL rt_thread_get_exit_code(HANDLE thread, DWORD *code) {
    if (!code) rt_fatal("GetExitCodeThread: lpExitCode is NULL");
    RtThreadObject *o = (RtThreadObject *)rt_handle_ref(thread, RT_K_THREAD);
    if (!o) return FALSE;                       /* last error: ERROR_INVALID_HANDLE, set there */
    rt_dispatcher_lock();
    DWORD c = o->exit_code;                     /* STILL_ACTIVE until step 7 of rt_thread_exit */
    rt_dispatcher_unlock();
    rt_object_unref(&o->obj);
    *code = c;
    return TRUE;
}
/* The seven THREAD_PRIORITY_ values of rt/win/process.h. */
static bool priority_is_documented(int priority) {
    switch (priority) {
    case -15: case -2: case -1: case 0: case 1: case 2: case 15:
        return true;
    default:
        return false;
    }
}

BOOL rt_thread_set_priority(HANDLE thread, int priority) {
    if (!priority_is_documented(priority))      /* fatal for any other value, whatever the handle */
        rt_fatal("SetThreadPriority: thread priority %d is not one of the seven documented values", priority);
    RtThreadObject *o = (RtThreadObject *)rt_handle_ref(thread, RT_K_THREAD);
    if (!o) return FALSE;                       /* last error: ERROR_INVALID_HANDLE, set there */
    rt_dispatcher_lock();
    o->priority = priority;
    rt_dispatcher_unlock();
    rt_object_unref(&o->obj);
    return TRUE;
}

int rt_thread_get_priority(HANDLE thread) {
    RtThreadObject *o = (RtThreadObject *)rt_handle_ref(thread, RT_K_THREAD);
    if (!o) return 0x7FFFFFFF;                  /* THREAD_PRIORITY_ERROR_RETURN; last error: ERROR_INVALID_HANDLE */
    rt_dispatcher_lock();
    int priority = o->priority;
    rt_dispatcher_unlock();
    rt_object_unref(&o->obj);
    return priority;
}
/* SetThreadIdealProcessor: the specification is rt/thread.h (unit rt-census-rows). */
DWORD rt_thread_set_ideal_processor(HANDLE thread, DWORD processor) {
    RtThreadObject *o = (RtThreadObject *)rt_handle_ref(thread, RT_K_THREAD);
    if (!o) return (DWORD)-1;                   /* last error: ERROR_INVALID_HANDLE, set there */
    const DWORD count = rt_machine()->cpu_count;
    if (processor > 64) {
        rt_object_unref(&o->obj);
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return (DWORD)-1;
    }
    if (processor >= count && processor != 64)
        rt_fatal("rt_thread_set_ideal_processor: not implemented: processor %u on a machine that reports %u",
                 (unsigned)processor, (unsigned)count);
    rt_dispatcher_lock();
    DWORD previous = o->ideal;
    if (processor != 64) o->ideal = processor;
    rt_dispatcher_unlock();
    rt_object_unref(&o->obj);
    return previous;
}
DWORD_PTR rt_thread_set_affinity(HANDLE thread, DWORD_PTR mask) {
    RtThreadObject *o = (RtThreadObject *)rt_handle_ref(thread, RT_K_THREAD);
    if (!o) return 0;                           /* last error: ERROR_INVALID_HANDLE, set there */
    RtProcessObject *proc = rt_process_object();
    rt_dispatcher_lock();
    /* SetProcessAffinityMask gives every thread the new process mask, including one that set its
     * own: the thread's own mask counts only if the process mask has not been set since it was
     * stored (affinity_epoch).  Otherwise the thread has the process's mask. */
    bool own = o->has_affinity && o->affinity_epoch == proc->affinity_changes;
    uint64_t previous = own ? o->affinity : proc->affinity;
    /* Windows (Vista and later) reads a mask whose low 32 bits are all set as "all processors":
     * the thread gets the process's mask.  Only the low 32 bits are looked at, so ~0 and ~0 >> 3
     * both do this.  Any other mask fails with ERROR_INVALID_PARAMETER if it is 0 or has a bit
     * outside the process mask. */
    uint64_t m = (uint64_t)mask;
    if ((uint32_t)m == 0xFFFFFFFFu) m = proc->affinity;
    bool valid = m != 0 && (m & ~proc->affinity) == 0;
    if (valid) {
        o->affinity = m;
        o->has_affinity = 1;
        o->affinity_epoch = proc->affinity_changes;
    }
    rt_dispatcher_unlock();
    rt_object_unref(&o->obj);
    if (!valid) {
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return (DWORD_PTR)previous;
}

HANDLE rt_thread_open(DWORD thread_id) {
    RtThreadObject *found = NULL;
    rt_thread_list_lock();
    for (RtThread *t = rt_thread_list_head(); t; t = t->next) {
        if (t->id == thread_id) {
            found = RT_THREAD_PRIV(t)->object;  /* set while the thread is in the list */
            rt_object_ref(&found->obj);
            break;
        }
    }
    rt_thread_list_unlock();
    if (!found) {
        rt_log_once(RT_CH_THREAD, RT_LL_WARN, "OpenThread", "OpenThread: no running thread has id 0x%x", thread_id);
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return rt_handle_new(&found->obj);          /* consumes the reference; NULL: ERROR_NOT_ENOUGH_MEMORY is set */
}

RtThread *rt_thread_from_handle(HANDLE thread, RtObject **object) {
    if (!object) rt_fatal("rt_thread_from_handle: object is NULL");
    *object = NULL;
    RtThreadObject *o = (RtThreadObject *)rt_handle_ref(thread, RT_K_THREAD);
    if (!o) return NULL;                        /* last error: ERROR_INVALID_HANDLE, set there */
    *object = &o->obj;                          /* keeps the reference from rt_handle_ref for the caller */
    return o->thread;
}
