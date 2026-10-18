/* The process: the priority class, the affinity masks, the error mode and the process times;
 * and one per-thread value, the execution state (docs/RUNTIME.md section 6.3).
 * Interface and specification: rt/thread.h, "the process".  Built: every function below.
 * Each takes the process object from rt_handle_ref(process, RT_K_PROCESS), which accepts
 * (HANDLE)-1 and open process handles and sets ERROR_INVALID_HANDLE itself otherwise; the
 * reference it returns is dropped before the function returns.  It may call rt_handle_ref,
 * rt_object_unref, rt_dispatcher_lock, rt_dispatcher_unlock, rt_system_affinity,
 * rt_thread_set_last_error and rt_fatal.  (Process exit is not here: rt/crash.h, core/exit.c.)
 *
 * The process object is made by core/thread.c (rt_process_object, thread_priv.h); its fields
 * are read and written under the DISPATCHER lock.
 */
#include <errno.h>
#include <string.h>
#include <sys/resource.h>

#include "thread_priv.h"

/* True for the six *_PRIORITY_CLASS values of rt/win/process.h. */
static bool is_priority_class(DWORD cls) {
    switch (cls) {
    case NORMAL_PRIORITY_CLASS:
    case IDLE_PRIORITY_CLASS:
    case HIGH_PRIORITY_CLASS:
    case REALTIME_PRIORITY_CLASS:
    case BELOW_NORMAL_PRIORITY_CLASS:
    case ABOVE_NORMAL_PRIORITY_CLASS:
        return true;
    default:
        return false;
    }
}

/* The background modes are not handled: fatal, after the reference is dropped. */
BOOL rt_process_set_priority_class(HANDLE process, DWORD cls) {
    RtProcessObject *o = (RtProcessObject *)rt_handle_ref(process, RT_K_PROCESS);
    if (!o) return FALSE;                       /* ERROR_INVALID_HANDLE, set by rt_handle_ref */
    const bool background = cls == PROCESS_MODE_BACKGROUND_BEGIN || cls == PROCESS_MODE_BACKGROUND_END;
    const bool valid = is_priority_class(cls);
    if (valid) {
        rt_dispatcher_lock();
        o->priority_class = cls;
        rt_dispatcher_unlock();
    }
    rt_object_unref(&o->obj);
    if (background)
        rt_fatal("SetPriorityClass: PROCESS_MODE_BACKGROUND_BEGIN and _END are not handled");
    if (!valid) {
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

DWORD rt_process_get_priority_class(HANDLE process) {
    RtProcessObject *o = (RtProcessObject *)rt_handle_ref(process, RT_K_PROCESS);
    if (!o) return 0;                           /* ERROR_INVALID_HANDLE, set by rt_handle_ref */
    rt_dispatcher_lock();
    const DWORD cls = o->priority_class;
    rt_dispatcher_unlock();
    rt_object_unref(&o->obj);
    return cls;
}

/* Both pointers are required: fatal when either is NULL.  The outputs are written on success only. */
BOOL rt_process_get_affinity(HANDLE process, DWORD_PTR *process_mask, DWORD_PTR *system_mask) {
    if (!process_mask || !system_mask)
        rt_fatal("GetProcessAffinityMask: a mask pointer is NULL");
    RtProcessObject *o = (RtProcessObject *)rt_handle_ref(process, RT_K_PROCESS);
    if (!o) return FALSE;                       /* ERROR_INVALID_HANDLE, set by rt_handle_ref */
    rt_dispatcher_lock();
    const uint64_t mask = o->affinity;
    rt_dispatcher_unlock();
    rt_object_unref(&o->obj);
    *process_mask = mask;
    *system_mask = rt_system_affinity();
    return TRUE;
}

/* Records `mask` and counts the change.  A mask of 0, or with a bit outside the system mask,
 * is ERROR_INVALID_PARAMETER. */
BOOL rt_process_set_affinity(HANDLE process, DWORD_PTR mask) {
    RtProcessObject *o = (RtProcessObject *)rt_handle_ref(process, RT_K_PROCESS);
    if (!o) return FALSE;                       /* ERROR_INVALID_HANDLE, set by rt_handle_ref */
    const uint64_t m = mask;
    const uint64_t sys = rt_system_affinity();
    const bool valid = m != 0 && (m & ~sys) == 0;
    if (valid) {
        rt_dispatcher_lock();
        o->affinity = m;
        o->affinity_changes++;
        rt_dispatcher_unlock();
    }
    rt_object_unref(&o->obj);
    if (!valid) {
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/* ---- the error mode ------------------------------------------------------------------------ */

/* SetErrorMode's one value.  Read and written only by the exchange below. */
static UINT error_mode;

UINT rt_process_swap_error_mode(UINT mode) {
    const UINT known = SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOALIGNMENTFAULTEXCEPT |
                       SEM_NOOPENFILEERRORBOX;
    if (mode & ~known)
        rt_fatal("SetErrorMode: mode 0x%x has a bit outside the four SEM_ values; what Windows does with it is not verified",
                 (unsigned)mode);
    if (mode & SEM_NOALIGNMENTFAULTEXCEPT)
        rt_fatal("SetErrorMode: mode 0x%x has SEM_NOALIGNMENTFAULTEXCEPT; what an x64 system records for it is not verified",
                 (unsigned)mode);
    return __atomic_exchange_n(&error_mode, mode, __ATOMIC_SEQ_CST);
}

/* ---- the process times --------------------------------------------------------------------- */

/* A timeval of getrusage as a FILETIME that holds a length of time: 100 ns units. */
static FILETIME filetime_from_timeval(struct timeval tv) {
    const uint64_t t = (uint64_t)tv.tv_sec * UINT64_C(10000000) + (uint64_t)tv.tv_usec * 10;
    return (FILETIME){ .dwLowDateTime = (DWORD)t, .dwHighDateTime = (DWORD)(t >> 32) };
}

BOOL rt_process_times(HANDLE process, FILETIME *creation, FILETIME *kernel, FILETIME *user) {
    if (!creation || !kernel || !user)
        rt_fatal("GetProcessTimes: a time pointer is NULL (creation %p, kernel %p, user %p)",
                 (void *)creation, (void *)kernel, (void *)user);
    RtProcessObject *o = (RtProcessObject *)rt_handle_ref(process, RT_K_PROCESS);
    if (!o) return FALSE;                       /* ERROR_INVALID_HANDLE, set by rt_handle_ref */
    const uint64_t made = o->creation_time;     /* written once, before the object was registered */
    rt_object_unref(&o->obj);

    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru))
        rt_fatal("GetProcessTimes: getrusage failed: %s", strerror(errno));
    *creation = (FILETIME){ .dwLowDateTime = (DWORD)made, .dwHighDateTime = (DWORD)(made >> 32) };
    *kernel = filetime_from_timeval(ru.ru_stime);
    *user = filetime_from_timeval(ru.ru_utime);
    return TRUE;
}

/* ---- the execution state of a thread -------------------------------------------------------- */

EXECUTION_STATE rt_thread_execution_state(Ctx *ctx, EXECUTION_STATE flags) {
    const EXECUTION_STATE known = ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED | ES_USER_PRESENT |
                                  ES_AWAYMODE_REQUIRED | ES_CONTINUOUS;
    if (!ctx || !ctx->host || !RT_THREAD_PRIV(RT_THREAD(ctx)))
        rt_fatal("SetThreadExecutionState: the Ctx is not a guest thread's");
    if (flags & ~known)
        rt_fatal("SetThreadExecutionState: flags 0x%x have a bit outside the five ES_ values; what Windows does with it is not verified",
                 (unsigned)flags);
    if (flags == ES_USER_PRESENT)
        rt_fatal("SetThreadExecutionState: ES_USER_PRESENT alone, which the documentation calls not supported; what Windows returns is not verified");
    if (flags & ES_USER_PRESENT) {              /* with any other flag: the documented failure */
        rt_set_last_error(ctx, ERROR_INVALID_PARAMETER);
        return 0;
    }
    if ((flags & ES_AWAYMODE_REQUIRED) && !(flags & ES_CONTINUOUS))
        rt_fatal("SetThreadExecutionState: flags 0x%x have ES_AWAYMODE_REQUIRED without ES_CONTINUOUS; what Windows does is not verified",
                 (unsigned)flags);

    RtThreadPriv *p = RT_THREAD_PRIV(RT_THREAD(ctx));
    const EXECUTION_STATE before = p->exec_state ? p->exec_state : ES_CONTINUOUS;
    if (flags & ES_CONTINUOUS) p->exec_state = flags;
    return before;
}
