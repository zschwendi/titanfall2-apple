/* What the call table needs from the driver beyond calling an export: a second guest
 * thread that runs host code, and functions of the driver that guest code can call (a
 * logging listener's methods).  gate2/host.c.
 *
 * A driver function that guest code calls is a native stub (rt_native_stub, rt/module.h): a
 * guest code address in ntdll.dll's native module that dispatches to a host function.  Stubs
 * can only be made before the dispatch table is sealed, so they are all made by
 * gate2_host_before_seal, which driver.c gives to rt_init.
 */
#ifndef GATE2_HOST_H
#define GATE2_HOST_H

#include "rt/thread.h"
#include "calls.h"

/* A host function that guest code calls: what a generated shim thunk does around a shim's
 * body (rt_shim_enter and rt_shim_leave in rt/shim.h), without a shim row.  Arguments are
 * read with ARG_* before any GUEST_CALL; the result goes in ctx->r[0].
 * (The Windows reference program, tools/ref, compiles the call tables against the real DLLs
 * and brings its own HOST_FN: there the function is called natively.) */
#ifndef HOST_FN
#define HOST_FN(name) \
    static void name##_body(Ctx *ctx); \
    static void name(Ctx *ctx) { \
        tf_fp_enter_host(); \
        RtThread *t_ = RT_THREAD(ctx); \
        uint32_t was_ = t_->in_host; \
        uint64_t rsp_ = ctx->r[4]; \
        t_->in_host = 1; \
        name##_body(ctx); \
        t_->in_host = was_; \
        ctx->r[4] = rsp_ + 8;                   /* the guest `ret` */ \
        tf_fp_enter_guest(ctx->mxcsr); \
    } \
    static void name##_body(Ctx *ctx)
#endif

/* RtConfig.before_seal: creates the driver's native stubs. */
void gate2_host_before_seal(void *user);

/* Run fn(c2, arg) on a new guest thread (rt_thread_create; the loader attaches it to both
 * modules first) and wait for the thread to end.  c2 is a copy of `c` with the new thread's
 * ctx; a FAIL or SKIP recorded in it is copied back to `c`.  Returns true when the thread
 * ran and recorded nothing.  *thread_id, if not NULL, gets the thread's id. */
bool gate2_on_thread(Call *c, void (*fn)(Call *c2, void *arg), void *arg, uint32_t *thread_id);

/* The same in two steps, for an entry that must do something while the second thread runs
 * (or is blocked): gate2_thread_start returns at once with a token (NULL after recording
 * FAIL); gate2_thread_finish waits up to 60 s for the thread to end and reports as
 * gate2_on_thread does. */
void *gate2_thread_start(Call *c, void (*fn)(Call *c2, void *arg), void *arg);
bool  gate2_thread_finish(Call *c, void *token);

/* calls_wide.c: its native stubs, made with the others in gate2_host_before_seal. */
void gate2_wide_before_seal(RtModule *ntdll);

/* A logging listener for tier0's LoggingSystem_RegisterLoggingListener: a guest-visible
 * object {vtable} whose Log slot (slot 1) records what it is given.  One per process. */
#define GATE2_LISTENER_KEPT 8
typedef struct {
    unsigned calls;                             /* times the Log slot was called (any thread) */
    unsigned slot0_calls;                       /* times slot 0 was called: not expected */
    uint32_t worst;                             /* the highest severity seen (0 message, 1 warning, ...) */
    bool     echo;                              /* set by the entry: each message is also a gate2_progress line */
    unsigned fails;                             /* messages that contain "FAIL" or "invalid value" */
    char     first_fail[200];                   /* the first of them */
    uint32_t channel[GATE2_LISTENER_KEPT], severity[GATE2_LISTENER_KEPT];  /* of the first calls */
    uint32_t colour[GATE2_LISTENER_KEPT];       /* the fourth dword of their contexts */
    char     text[GATE2_LISTENER_KEPT][200];    /* their messages */
} Gate2Listener;

/* The guest address of the listener object; `rec` is cleared and receives the calls. */
uint64_t gate2_listener(Gate2Listener *rec);

#endif /* GATE2_HOST_H */
