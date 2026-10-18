/* CreateThread, the start trampoline, ExitThread and ResumeThread (docs/RUNTIME.md sections
 * 6.1 and 6.3).  Interface and specification: rt/thread.h.  The building blocks
 * (rt_thread_new, rt_thread_bind, rt_thread_finish) are in thread.c.
 */
#include <pthread.h>
#include <pthread/qos.h>
#include <setjmp.h>

#include "thread_priv.h"
#include "rt/crash.h"            /* rt_process_exit */
#include "rt/module.h"           /* rt_loader_in_entry */

#define MIN_GUEST_STACK   (UINT64_C(1) << 20)     /* section 5.3: at least 1 MiB */
#define MAX_GUEST_STACK   (UINT64_C(1) << 30)     /* this runtime's limit */
#define MIN_HOST_STACK    (UINT64_C(4) << 20)     /* section 6.1 step 4: four times the guest stack, */
#define HOST_STACK_RATIO  4                       /* at least 4 MiB (E5) */
#define KNOWN_FLAGS       (CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION)

/* Host threads started here that have not finished (rt_thread_host_threads).  Atomic. */
static uint32_t host_threads;

unsigned rt_thread_host_threads(void) { return __atomic_load_n(&host_threads, __ATOMIC_SEQ_CST); }

/* The base frame of a created thread: everything it does on its own host thread. */
static void *thread_main(void *arg) {
    RtThread *t = arg;
    RtThreadPriv *p = RT_THREAD_PRIV(t);
    DWORD code;

    rt_thread_bind(t);

    pthread_mutex_lock(&p->gate_lock);          /* created suspended: until the first ResumeThread */
    while (p->suspend_count) pthread_cond_wait(&p->gate_cond, &p->gate_lock);
    pthread_mutex_unlock(&p->gate_lock);

    rt_log(RT_CH_THREAD, RT_LL_INFO, "guest thread 0x%x starts: thread block %#llx, stack %#llx-%#llx", t->id,
           (unsigned long long)t->teb, (unsigned long long)t->stack_limit, (unsigned long long)t->stack_base);

    if (setjmp(p->exit_jmp) == 0) {
        p->exit_armed = 1;
        rt_loader_thread_attach(&t->ctx);
        code = (DWORD)rt_guest_call(&t->ctx, p->start, 1, &p->param);
    } else {
        /* rt_thread_exit jumped here from some depth of guest calls and shims.  The host
         * frames in between are gone; put the thread back in the state of host code that
         * is in no shim.  The guest rsp stays where ExitThread was called. */
        code = p->exit_code;
        t->shim = NULL;
        t->in_host = 0;
        tf_fp_enter_host();
    }
    p->exit_armed = 0;
    rt_thread_finish(t, code);
    __atomic_fetch_sub(&host_threads, 1, __ATOMIC_SEQ_CST);     /* the last thing this host thread does */
    return NULL;
}

HANDLE rt_thread_create(Ctx *ctx, SIZE_T stack_size, GUESTFN start, uint64_t param, DWORD flags, DWORD *thread_id) {
    if (!ctx || !RT_THREAD(ctx) || RT_THREAD(ctx) != rt_thread_self())
        rt_fatal("rt_thread_create: the ctx is not the calling guest thread's");
    if (flags & ~KNOWN_FLAGS)
        rt_fatal("rt_thread_create: creation flags %#x are not handled (only CREATE_SUSPENDED and "
                 "STACK_SIZE_PARAM_IS_A_RESERVATION are)", (unsigned)(flags & ~KNOWN_FLAGS));
    if (!start) rt_fatal("rt_thread_create: the start address is 0");
    if (stack_size > MAX_GUEST_STACK) return FAIL(ctx, ERROR_NOT_ENOUGH_MEMORY, NULL);

    uint64_t guest_stack = stack_size < MIN_GUEST_STACK ? MIN_GUEST_STACK : stack_size;
    guest_stack = (guest_stack + 0xFFFF) & ~UINT64_C(0xFFFF);
    uint64_t host_stack = guest_stack * HOST_STACK_RATIO;
    if (host_stack < MIN_HOST_STACK) host_stack = MIN_HOST_STACK;

    RtThread *t = rt_thread_new(guest_stack, RT_TK_CREATED);
    if (!t) return NULL;                        /* last error: ERROR_NOT_ENOUGH_MEMORY */
    RtThreadPriv *p = RT_THREAD_PRIV(t);
    const DWORD id = t->id;
    p->start = start;
    p->param = param;
    p->suspend_count = (flags & CREATE_SUSPENDED) ? 1 : 0;

    /* The handle first: once the host thread exists nothing can be taken back. */
    rt_object_ref(&p->object->obj);
    HANDLE h = rt_handle_new(&p->object->obj);  /* consumes that reference, also when it fails */
    if (!h) {
        rt_thread_discard(t);
        return FAIL(ctx, ERROR_NOT_ENOUGH_MEMORY, NULL);
    }

    pthread_attr_t attr;
    pthread_t host;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (size_t)host_stack);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INTERACTIVE, 0);     /* section 6.3 */
    __atomic_fetch_add(&host_threads, 1, __ATOMIC_SEQ_CST);
    int e = pthread_create(&host, &attr, thread_main, t);
    pthread_attr_destroy(&attr);
    if (e) {
        __atomic_fetch_sub(&host_threads, 1, __ATOMIC_SEQ_CST);
        rt_log(RT_CH_THREAD, RT_LL_WARN, "CreateThread: the host refused a thread (errno %d)", e);
        rt_handle_close(h);
        rt_thread_discard(t);
        return FAIL(ctx, rt_win32_from_errno(e, RT_ERR_THREAD), NULL);
    }
    /* From here `t` belongs to the new thread, which may already have ended. */
    if (thread_id) *thread_id = id;
    return h;
}

_Noreturn void rt_thread_exit(Ctx *ctx, DWORD code) {
    RtThread *t = ctx ? RT_THREAD(ctx) : NULL;
    if (!t || t != rt_thread_self())
        rt_fatal("rt_thread_exit: the ctx is not the calling guest thread's");
    RtThreadPriv *p = RT_THREAD_PRIV(t);
    if (p->finishing)
        rt_fatal("rt_thread_exit: called a second time for thread 0x%x, from an FLS callback or "
                 "DllMain(DLL_THREAD_DETACH)", t->id);
    if (p->kind == RT_TK_MAIN) {
        unsigned others = rt_thread_count() - 1;
        if (others)
            rt_fatal("ExitThread on guest thread 1 while %u other guest thread%s still run%s: on Windows the "
                     "process would live on without its first thread, which the host's main thread cannot do",
                     others, others == 1 ? "" : "s", others == 1 ? "s" : "");
        rt_process_exit(ctx, code);
    }
    if (p->kind != RT_TK_CREATED || !p->exit_armed)
        rt_fatal("rt_thread_exit on thread 0x%x, a host thread the runtime did not create (rt_thread_attach): "
                 "there is no frame to unwind to; end it with rt_thread_detach", t->id);
    /* The loader called this entry point or TLS callback under its lock, from a host frame
     * the jump below would discard with its unlock: every later load and every later thread
     * start would wait for ever (docs/handoff/rt-loader-load.md).  Guest thread 1 does not
     * come here: for it ExitThread ends the process, above. */
    const RtModule *in = NULL;
    DWORD reason = 0;
    if (rt_loader_in_entry(t, &in, &reason))
        rt_fatal("ExitThread on thread 0x%x inside the entry point or a TLS callback of %s (reason %u): that is "
                 "not supported, the loader lock taken for the call would stay held", t->id, in->name, reason);
    p->exit_code = code;
    longjmp(p->exit_jmp, 1);
}

DWORD rt_thread_resume(HANDLE thread) {
    RtThreadObject *o = (RtThreadObject *)rt_handle_ref(thread, RT_K_THREAD);
    if (!o) return (DWORD)-1;                   /* last error: ERROR_INVALID_HANDLE */
    rt_dispatcher_lock();
    bool exited = o->exited != 0;
    rt_dispatcher_unlock();
    if (exited) {
        /* Windows answers 0 for a thread that has ended, and nothing happens (Wine
         * kernel32/tests/thread.c, test_CreateThread_suspended).  Last error is left alone: what
         * Windows sets for this call is not known. */
        rt_object_unref(&o->obj);
        return 0;
    }
    RtThreadPriv *p = RT_THREAD_PRIV(o->thread);
    pthread_mutex_lock(&p->gate_lock);
    DWORD previous = p->suspend_count;
    if (previous) {
        p->suspend_count = 0;
        pthread_cond_broadcast(&p->gate_cond);
    }
    pthread_mutex_unlock(&p->gate_lock);
    rt_object_unref(&o->obj);
    return previous;
}
