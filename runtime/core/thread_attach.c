/* Host threads the runtime did not create: rt_thread_attach and rt_thread_detach
 * (docs/RUNTIME.md section 6.1).  Interface and specification: rt/thread.h.
 *
 * No state of its own: both are compositions of the building blocks of thread_priv.h
 * (rt_thread_new with RT_TK_ATTACHED, rt_thread_bind, rt_thread_finish).
 */
#include "thread_priv.h"

#define ATTACHED_GUEST_STACK  (UINT64_C(1) << 20)   /* the 1 MiB guest stack of rt/thread.h */

RtThread *rt_thread_attach(const char *name) {
    RtThread *self = rt_thread_self();
    if (self) return self;                      /* already a guest thread: that one, name ignored */

    RtThread *t = rt_thread_new(ATTACHED_GUEST_STACK, RT_TK_ATTACHED);
    if (!t) rt_fatal("rt_thread_attach: no memory for a guest thread");
    if (name) rt_thread_set_name(t, name);      /* before the bind, which hands the name to the host thread */
    rt_thread_bind(t);
    rt_loader_thread_attach(&t->ctx);
    return t;
}

void rt_thread_detach(RtThread *t) {
    if (!t || t != rt_thread_self())
        rt_fatal("rt_thread_detach: not the calling thread's own RtThread");
    if (RT_THREAD_PRIV(t)->kind != RT_TK_ATTACHED)
        rt_fatal("rt_thread_detach: thread 0x%x was not made by rt_thread_attach", t->id);
    if (t->shim)
        rt_fatal("rt_thread_detach: called from inside a shim");
    rt_thread_finish(t, 0);                     /* steps 1 to 8 of rt_thread_exit, exit code 0 */
}
