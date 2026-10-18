/* CreateThread, the start trampoline, ExitThread and ResumeThread (runtime/core/thread_run.c
 * and the building blocks in thread.c; the specification is rt/thread.h).
 *
 * The threads here are real guest threads made by rt_thread_create.  Their start routines are
 * GUEST_FN functions: host code that runs on the new thread with its Ctx.  They record what
 * they see in statics and the main thread checks it after joining (only the main thread may
 * use the T_ macros).  Nothing asserts that something happens quickly: a test waits for a
 * state it can observe (EVENTUALLY, a wait on a handle), as tests/core/wait_util.c explains.
 */
#include "wait_util.c"
#include "rt/vm.h"
#include "../../core/thread_priv.h"

#define SENTINEL 0x5EA1u

static RtThreadObject *object_of(HANDLE h) { return (RtThreadObject *)wu_obj(h); }
static DWORD join(HANDLE h) { return rt_wait_handle(T_CTX(), h, INFINITE, FALSE); }
static bool is_free(uint64_t addr) {
    MEMORY_BASIC_INFORMATION mbi;
    return rt_vm_query(addr, &mbi) && mbi.State == MEM_FREE;
}

/* ---- what a new thread sees -------------------------------------------------------------- */

static struct Seen {
    RtThread *self, *from_ctx;
    RtObject *pseudo;
    uint64_t  param, rsp, teb, teb_self, teb_pid, teb_tid, teb_tls, stack_base, stack_limit, stack_region;
    uint64_t  fpcr, tls_or;
    uint32_t  id, mxcsr, last_error, in_host, count;
    int       ran;
} seen;

GUEST_FN(record)
{
    RtThread *t = RT_THREAD(ctx);
    uint64_t teb = ctx->gs_base;
    seen.self = rt_thread_self();
    seen.from_ctx = t;
    seen.param = ctx->r[1];
    seen.rsp = ctx->r[4];
    seen.teb = teb;
    seen.teb_self = LD64(teb + RT_TEB_SELF);
    seen.teb_pid = LD64(teb + RT_TEB_PROCESS_ID);
    seen.teb_tid = LD64(teb + RT_TEB_THREAD_ID);
    seen.teb_tls = LD64(teb + RT_TEB_TLS_POINTER);
    seen.stack_base = LD64(teb + RT_TEB_STACK_BASE);
    seen.stack_limit = LD64(teb + RT_TEB_STACK_LIMIT);
    seen.stack_region = LD64(teb + RT_TEB_DEALLOC_STACK);
    seen.tls_or = LD64(teb + RT_TEB_TLS_EXPANSION);
    for (unsigned i = 0; i < RT_TLS_MINIMUM_SLOTS; i++) seen.tls_or |= LD64(teb + RT_TEB_TLS_SLOTS + 8 * (uint64_t)i);
    seen.fpcr = tf_fpcr_read();
    seen.id = t->id;
    seen.mxcsr = ctx->mxcsr;
    seen.last_error = rt_get_last_error(ctx);
    seen.in_host = t->in_host;
    seen.count = rt_thread_count();
    seen.pseudo = rt_handle_ref(RT_PSEUDO_THREAD, RT_K_THREAD);
    if (seen.pseudo) rt_object_unref(seen.pseudo);
    rt_set_last_error(ctx, ERROR_NOT_SUPPORTED);        /* the thread's own; the creator must not see it */
    ST64(seen.stack_limit, 0x1234);                     /* the whole stack is usable */
    ST64(seen.stack_base - 8, 0x5678);
    wu_store(&seen.ran, 1);
    ctx->r[0] = 0xDEAD00000007;                         /* the exit code is the low 32 bits */
    GUEST_RET(ctx);
}

TEST(core, thread_create_runs_the_start_routine)
{
    Ctx *ctx = T_CTX();
    DWORD id = 0;
    memset(&seen, 0, sizeof seen);
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_thread_count(), 1);
    HANDLE h = rt_thread_create(ctx, 0, T_GUEST_ADDR(record), 0x1122334455667788, 0, &id);
    T_REQUIRE(h != NULL);
    RtThreadObject *o = object_of(h);
    RtThread *t = o->thread;
    T_EQ(o->obj.kind, RT_K_THREAD);
    T_EQ(o->id, id);
    T_EQ(join(h), WAIT_OBJECT_0);
    T_EQ(T_LAST_ERROR(), SENTINEL);                     /* success leaves the creator's last error alone */

    /* On the new thread. */
    T_EQ(seen.ran, 1);
    T_TRUE(seen.self == t && seen.from_ctx == t);
    T_EQ(seen.param, 0x1122334455667788);
    T_EQ(seen.rsp & 15, 8);                             /* the Windows alignment at function entry */
    T_TRUE(seen.rsp > seen.stack_limit && seen.rsp < seen.stack_base);
    T_EQ(seen.id, id);
    T_TRUE(id % 4 == 0 && id > RT_FIRST_THREAD_ID);
    T_TRUE(rt_vm_in_vm_arena(seen.teb) && (seen.teb & 0xFFFF) == 0);
    T_EQ(seen.teb_self, seen.teb);
    T_EQ(seen.teb_pid, RT_PROCESS_ID);
    T_EQ(seen.teb_tid, id);
    T_EQ(seen.teb_tls, seen.teb + RT_TEB_STATIC_TLS_ARRAY);
    T_EQ(seen.tls_or, 0);                               /* every TLS slot and the expansion pointer are zero */
    T_EQ(seen.stack_base - seen.stack_limit, UINT64_C(1) << 20);    /* 0 asks for the 1 MiB minimum */
    T_EQ(seen.stack_limit - seen.stack_region, 0x10000);
    T_EQ(seen.mxcsr, 0x1F80);
    T_EQ(seen.fpcr, tf_fpcr_for_mxcsr(0x1F80));         /* guest code runs in the guest float mode */
    T_EQ(seen.in_host, 0);
    T_EQ(seen.last_error, 0);
    T_EQ(seen.count, 2);
    T_TRUE(seen.pseudo == &o->obj);                     /* (HANDLE)-2 on that thread is its own object */

    /* After the wait returned (steps 5 to 7 of rt_thread_exit are done). */
    T_EQ(rt_thread_count(), 1);
    T_EQ(o->exited, 1);
    T_EQ(o->exit_code, 7);
    T_EQ(t->teb, 0);
    T_EQ(t->ctx.gs_base, 0);
    T_TRUE(is_free(seen.teb));
    T_TRUE(is_free(seen.stack_region) && is_free(seen.stack_base - 8));
    T_TRUE(EVENTUALLY(t->sub[RT_SUB_HANDLE] == NULL));  /* step 8 follows the signal */
    T_EQ(join(h), WAIT_OBJECT_0);                       /* a thread object stays signalled: nothing is taken */
    T_EQ(rt_wait_handle(ctx, h, 0, FALSE), WAIT_OBJECT_0);
    T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
    T_EQ(rt_handle_close(h), TRUE);
}

/* ---- a thread that runs until it is told to stop ------------------------------------------- */

static HANDLE go;               /* manual-reset event the worker waits for */
static int    started, after_exit;
static DWORD  worker_wait;

GUEST_FN(wait_for_go)
{
    wu_store(&started, 1);
    __atomic_store_n(&worker_wait, rt_wait_handle(ctx, go, INFINITE, FALSE), __ATOMIC_SEQ_CST);   /* two workers store it */
    ctx->r[0] = ctx->r[1];      /* returns its parameter */
    GUEST_RET(ctx);
}

static void count_threads(RtThread *t, void *user) {
    uint32_t *ids = user;
    if (ids[0] < 7) ids[++ids[0]] = t->id;
}

TEST(core, thread_object_is_signalled_only_at_exit)
{
    Ctx *ctx = T_CTX();
    DWORD id = 0, id2 = 0;
    go = rt_event_create(ctx, TRUE, FALSE, NULL);
    wu_store(&started, 0);
    HANDLE h = rt_thread_create(ctx, 0x180001, T_GUEST_ADDR(wait_for_go), 99, STACK_SIZE_PARAM_IS_A_RESERVATION, &id);
    T_REQUIRE(h != NULL);
    RtThreadObject *o = object_of(h);
    T_TRUE(EVENTUALLY(wu_load(&started)));
    T_EQ(rt_wait_handle(ctx, h, 0, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_wait_handle(ctx, h, 20, FALSE), WAIT_TIMEOUT);
    T_EQ(o->exited, 0);
    T_EQ(o->exit_code, STILL_ACTIVE);
    T_EQ(o->priority, THREAD_PRIORITY_NORMAL);
    T_EQ(rt_thread_count(), 2);
    T_TRUE(o->thread->sub[RT_SUB_HANDLE] == &o->obj);
    T_EQ(o->thread->stack_base - o->thread->stack_limit, 0x190000);     /* rounded up to 64 KB */

    /* The list, newest first; a second thread gets another id. */
    uint32_t ids[8] = { 0 };
    rt_thread_for_each(count_threads, ids);
    T_EQ(ids[0], 2);
    T_EQ(ids[1], id);
    T_EQ(ids[2], RT_FIRST_THREAD_ID);
    HANDLE h2 = rt_thread_create(ctx, 0, T_GUEST_ADDR(wait_for_go), 5, 0, &id2);
    T_REQUIRE(h2 != NULL);
    T_TRUE(id2 != id && id2 % 4 == 0 && id2 > RT_FIRST_THREAD_ID);
    T_EQ(rt_thread_count(), 3);

    /* ResumeThread on threads that are not suspended. */
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_thread_resume(h), 0);
    T_EQ(rt_thread_resume(RT_PSEUDO_THREAD), 0);
    T_EQ(T_LAST_ERROR(), SENTINEL);

    /* Closing the handle of a running thread does not disturb it. */
    T_EQ(rt_handle_close(h2), TRUE);
    T_EQ(rt_event_set(ctx, go), TRUE);
    HANDLE both[1] = { h };
    T_EQ(rt_wait_handles(ctx, 1, both, TRUE, INFINITE, FALSE), WAIT_OBJECT_0);
    T_EQ(o->exit_code, 99);
    T_EQ(__atomic_load_n(&worker_wait, __ATOMIC_SEQ_CST), WAIT_OBJECT_0);
    T_TRUE(EVENTUALLY(rt_thread_count() == 1));         /* the thread whose handle was closed ends too */
    /* Wait until the thread's host thread has finished: the thread has ended for the resume below. */
    T_REQUIRE(EVENTUALLY(rt_thread_host_threads() == 0));
    /* Windows: ResumeThread on a thread that has ended gives 0 and does nothing (Wine
     * kernel32/tests/thread.c, test_CreateThread_suspended).  Last error is not asserted there;
     * the runtime leaves it alone, and this checks that. */
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_thread_resume(h), 0);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_EQ(rt_thread_count(), 1);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(rt_handle_close(go), TRUE);
}

/* ---- ExitThread from inside guest calls ---------------------------------------------------- */

GUEST_FN(exit_42)
{
    rt_thread_exit(ctx, ctx->r[1] == 1 ? 42 : 259);
}

GUEST_FN(calls_exit_two_deep)
{
    uint64_t arg = ctx->r[1];
    rt_set_last_error(ctx, ERROR_NOT_SUPPORTED);
    RT_THREAD(ctx)->in_host = 1;                        /* as inside a shim that calls back into the guest */
    rt_guest_call(ctx, T_GUEST_ADDR(exit_42), 1, &arg);
    wu_store(&after_exit, 1);                           /* never reached */
    ctx->r[0] = 1;
    GUEST_RET(ctx);
}

GUEST_FN(owns_a_mutex_and_returns)
{
    go = rt_mutex_create(ctx, TRUE, NULL);              /* owned by this thread, never released */
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

TEST(core, thread_exit_unwinds_and_abandons)
{
    Ctx *ctx = T_CTX();
    wu_store(&after_exit, 0);
    HANDLE h = rt_thread_create(ctx, 0, T_GUEST_ADDR(calls_exit_two_deep), 1, 0, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(join(h), WAIT_OBJECT_0);
    T_EQ(object_of(h)->exit_code, 42);
    T_EQ(wu_load(&after_exit), 0);
    T_EQ(rt_thread_count(), 1);
    T_EQ(rt_handle_close(h), TRUE);

    /* An exit code of 259 reads as STILL_ACTIVE although the thread has ended, as on Windows. */
    h = rt_thread_create(ctx, 0, T_GUEST_ADDR(calls_exit_two_deep), 2, 0, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(join(h), WAIT_OBJECT_0);
    T_EQ(object_of(h)->exited, 1);
    T_EQ(object_of(h)->exit_code, STILL_ACTIVE);
    T_EQ(rt_handle_close(h), TRUE);

    /* A mutex the thread still owns is abandoned BEFORE its object is signalled: whoever
     * has waited for the thread finds the mutex abandoned, never still owned. */
    go = NULL;
    h = rt_thread_create(ctx, 0, T_GUEST_ADDR(owns_a_mutex_and_returns), 0, 0, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(join(h), WAIT_OBJECT_0);
    T_REQUIRE(go != NULL);
    T_EQ(rt_wait_handle(ctx, go, 0, FALSE), WAIT_ABANDONED_0);
    T_EQ(rt_mutex_release(ctx, go), TRUE);
    T_EQ(rt_handle_close(go), TRUE);
    T_EQ(rt_handle_close(h), TRUE);
}

/* ---- created suspended ---------------------------------------------------------------------- */

TEST(core, thread_created_suspended_waits_for_resume)
{
    Ctx *ctx = T_CTX();
    DWORD id = 0;
    go = rt_event_create(ctx, TRUE, FALSE, NULL);
    wu_store(&started, 0);
    HANDLE h = rt_thread_create(ctx, 0, T_GUEST_ADDR(wait_for_go), 3, CREATE_SUSPENDED, &id);
    T_REQUIRE(h != NULL);
    T_EQ(rt_thread_count(), 2);                         /* it is in the list although it runs nothing */
    wu_settle();
    T_EQ(wu_load(&started), 0);
    T_EQ(rt_wait_handle(ctx, h, 0, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_thread_resume(h), 1);                       /* the count before the call */
    T_TRUE(EVENTUALLY(wu_load(&started)));
    T_EQ(rt_thread_resume(h), 0);
    T_EQ(rt_event_set(ctx, go), TRUE);
    T_EQ(join(h), WAIT_OBJECT_0);
    T_EQ(object_of(h)->exit_code, 3);

    /* Not a thread handle. */
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_thread_resume(go), (DWORD)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_thread_resume(NULL), (DWORD)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_handle_close(h), TRUE);
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_thread_resume(h), (DWORD)-1);               /* a closed handle */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_handle_close(go), TRUE);
}

/* ---- failures and stops ---------------------------------------------------------------------- */

TEST(core, thread_create_failures)
{
    Ctx *ctx = T_CTX();
    DWORD id = 0xABCD;
    Ctx stranger;
    memset(&stranger, 0, sizeof stranger);

    rt_set_last_error(ctx, SENTINEL);
    T_TRUE(rt_thread_create(ctx, (UINT64_C(1) << 30) + 1, T_GUEST_ADDR(record), 0, 0, &id) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    T_EQ(id, 0xABCD);                                   /* not written on failure */
    T_EQ(rt_thread_count(), 1);                         /* nothing was left behind */
    T_EQ(rt_thread_host_threads(), 0);

    T_TRAPS(rt_thread_create(ctx, 0, T_GUEST_ADDR(record), 0, 0x8, NULL), "creation flags 0x8 are not handled");
    T_TRAPS(rt_thread_create(ctx, 0, T_GUEST_ADDR(record), 0, CREATE_SUSPENDED | 0x80000000u, NULL),
            "creation flags 0x80000000 are not handled");
    T_TRAPS(rt_thread_create(ctx, 0, 0, 0, 0, NULL), "the start address is 0");
    T_TRAPS(rt_thread_create(&stranger, 0, T_GUEST_ADDR(record), 0, 0, NULL), "not the calling guest thread's");
    T_TRAPS(rt_thread_exit(&stranger, 0), "not the calling guest thread's");

    /* ExitThread on guest thread 1 while another thread runs is a stop. */
    go = rt_event_create(ctx, TRUE, FALSE, NULL);
    wu_store(&started, 0);
    HANDLE h = rt_thread_create(ctx, 0, T_GUEST_ADDR(wait_for_go), 0, 0, NULL);
    T_REQUIRE(h != NULL);
    T_TRUE(EVENTUALLY(wu_load(&started)));
    /* The forks below must find the other thread asleep in its wait, where it holds no lock,
     * and not on its way there (the handle table's lock, the dispatcher's). */
    T_REQUIRE(EVENTUALLY(rt_wait_queue_length(wu_obj(go)) == 1));
    T_TRAPS(rt_thread_exit(ctx, 0), "ExitThread on guest thread 1 while 1 other guest thread still runs");
    T_TRAPS(rt_thread_finish(RT_THREAD(ctx), 0), "guest thread 1 ends only with the process");
    T_EQ(rt_event_set(ctx, go), TRUE);
    T_EQ(join(h), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(rt_handle_close(go), TRUE);
}

/* ---- many threads at once -------------------------------------------------------------------- */

#define MANY 24
static uint32_t tally;

GUEST_FN(add_and_return_index)
{
    __atomic_fetch_add(&tally, (uint32_t)ctx->r[1] + 1, __ATOMIC_SEQ_CST);
    if (ctx->r[1] & 1) rt_thread_exit(ctx, (DWORD)ctx->r[1] + 1000);   /* half leave through ExitThread */
    ctx->r[0] = ctx->r[1] + 1000;
    GUEST_RET(ctx);
}

TEST(core, thread_many_create_and_join)
{
    Ctx *ctx = T_CTX();
    for (unsigned round = 0; round < 4; round++) {
        HANDLE h[MANY];
        DWORD id[MANY];
        __atomic_store_n(&tally, 0, __ATOMIC_SEQ_CST);
        for (unsigned i = 0; i < MANY; i++) {
            h[i] = rt_thread_create(ctx, 0, T_GUEST_ADDR(add_and_return_index), i, 0, &id[i]);
            T_REQUIRE(h[i] != NULL);
        }
        T_EQ(rt_wait_handles(ctx, MANY, h, TRUE, INFINITE, FALSE), WAIT_OBJECT_0);
        T_EQ(__atomic_load_n(&tally, __ATOMIC_SEQ_CST), MANY * (MANY + 1) / 2);
        T_EQ(rt_thread_count(), 1);
        for (unsigned i = 0; i < MANY; i++) {
            T_REQUIRE(object_of(h[i])->exit_code == i + 1000);
            for (unsigned k = 0; k < i; k++) T_REQUIRE(id[k] != id[i]);    /* ids are never reused */
            T_REQUIRE(rt_handle_close(h[i]) == TRUE);
        }
    }
}
