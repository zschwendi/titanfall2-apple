/* rt_thread_attach and rt_thread_detach: host threads the runtime did not create (the unit
 * core/thread_attach.c; the specification is rt/thread.h, "host threads the runtime did not
 * create").  The numbered plan is the contract: every case is one T_PLAN block.
 *
 * The host threads are real pthreads.  Each records what it saw in a static; the main thread
 * (the one that runs the TEST body) checks the record after pthread_join.  Only the main thread
 * uses the T_ macros.
 */
#include "rt/test.h"
#include "wait_util.c"
#include "../../core/thread_priv.h"

#define SENTINEL 0x5EA1u

static RtThreadObject *object_of(HANDLE h) { return (RtThreadObject *)wu_obj(h); }

GUEST_FN(add_one)
{
    ctx->r[0] = ctx->r[1] + 1;
    GUEST_RET(ctx);
}

/* ---- case 1: the attach itself ----------------------------------------------------------------- */

static struct {
    RtThread *self_before, *t, *self_after;
    int       name_ok;
    uint64_t  id, stack_span, teb, teb_self, teb_id;
    uint32_t  count, kind, obj_kind;
} c1;

static void *case1_host(void *arg)
{
    RtThread *t;
    (void)arg;
    c1.self_before = rt_thread_self();
    t = rt_thread_attach("audio");
    c1.t = t;
    c1.self_after = rt_thread_self();
    if (t) {
        uint64_t teb = (uint64_t)t->teb;
        c1.name_ok = strcmp(t->name, "audio") == 0;
        c1.id = t->id;
        c1.stack_span = t->stack_base - t->stack_limit;
        c1.teb = teb;
        c1.teb_self = LD64(teb + RT_TEB_SELF);
        c1.teb_id = LD64(teb + RT_TEB_THREAD_ID);
        c1.count = rt_thread_count();
        c1.kind = RT_THREAD_PRIV(t)->kind;
        c1.obj_kind = ((RtObject *)t->sub[RT_SUB_HANDLE])->kind;
        rt_thread_detach(t);
    }
    return NULL;
}

/* ---- case 2: guest code on the attached thread ------------------------------------------------- */

static struct { uint64_t arg, got; } c2;

static void *case2_host(void *p)
{
    RtThread *t;
    uint64_t arg = 41;
    (void)p;
    t = rt_thread_attach("audio");
    if (t) {
        c2.arg = arg;
        c2.got = rt_guest_call(&t->ctx, T_GUEST_ADDR(add_one), 1, &arg);
        rt_thread_detach(t);
    }
    return NULL;
}

/* ---- case 3: a second attach on a thread that is a guest thread already ------------------------ */

static struct { RtThread *first, *second; int name_ok; uint32_t count; } c3;

static void *case3_host(void *p)
{
    (void)p;
    c3.first = rt_thread_attach("audio");
    if (c3.first) {
        c3.second = rt_thread_attach("other");
        c3.name_ok = strcmp(c3.first->name, "audio") == 0;
        c3.count = rt_thread_count();
        rt_thread_detach(c3.first);
    }
    return NULL;
}

/* ---- case 4: the thread's handle outlives its detach -------------------------------------------- */

static struct { HANDLE hp; RtThread *self_after; uint32_t count; } c4;

static void *case4_host(void *p)
{
    RtThread *t;
    (void)p;
    t = rt_thread_attach("audio");
    if (t) {
        c4.hp = rt_handle_dup(RT_PSEUDO_THREAD);
        rt_thread_detach(t);
        c4.self_after = rt_thread_self();
        c4.count = rt_thread_count();
    }
    return NULL;
}

/* ---- case 5: no name ----------------------------------------------------------------------------- */

static struct { RtThread *t; char first; } c5;

static void *case5_host(void *p)
{
    (void)p;
    c5.t = rt_thread_attach(NULL);
    if (c5.t) {
        c5.first = c5.t->name[0];
        rt_thread_detach(c5.t);
    }
    return NULL;
}

/* ---- case 7: the exit path abandons a mutex the thread still owns ------------------------------- */

static struct { HANDLE m; } c7;

static void *case7_host(void *p)
{
    RtThread *t;
    (void)p;
    t = rt_thread_attach(NULL);
    if (t) {
        c7.m = rt_mutex_create(&t->ctx, TRUE, NULL);
        rt_thread_detach(t);
    }
    return NULL;
}

/* ---- case 9: detach from another thread is refused; the owner detaches later ------------------- */

static struct {
    pthread_mutex_t lock;
    pthread_cond_t  cv;
    RtThread       *t;
    int             attached, released;
} c9 = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, 0, 0 };

static void *case9_host(void *p)
{
    RtThread *t;
    (void)p;
    t = rt_thread_attach("audio");
    pthread_mutex_lock(&c9.lock);
    c9.t = t;
    c9.attached = 1;
    pthread_cond_broadcast(&c9.cv);
    while (!c9.released) pthread_cond_wait(&c9.cv, &c9.lock);
    pthread_mutex_unlock(&c9.lock);
    if (t) rt_thread_detach(t);
    return NULL;
}

/* ---- cases 10 and 11: the fatal paths, run under T_TRAPS in a forked child ----------------------- */

static void *case10_host(void *p)
{
    RtThread *t;
    (void)p;
    t = rt_thread_attach(NULL);
    rt_thread_exit(&t->ctx, 0);
    return NULL;
}

static void f10(void)
{
    pthread_t p;
    pthread_create(&p, NULL, case10_host, NULL);
    pthread_join(p, NULL);
}

static void *case11_host(void *p)
{
    RtThread *t;
    RtShimFrame frame;
    (void)p;
    t = rt_thread_attach(NULL);
    memset(&frame, 0, sizeof frame);
    t->shim = &frame;
    rt_thread_detach(t);
    return NULL;
}

static void g11(void)
{
    pthread_t p;
    pthread_create(&p, NULL, case11_host, NULL);
    pthread_join(p, NULL);
}

/* ---- case 12: eight host threads, each 200 rounds of attach, guest call, detach ---------------- */

#define WORKERS 8
#define ROUNDS  200

static struct { uint32_t ok[WORKERS], bad[WORKERS]; } c12;

static void *case12_host(void *p)
{
    unsigned w = (unsigned)(uintptr_t)p;
    for (unsigned r = 0; r < ROUNDS; r++) {
        uint64_t a = (uint64_t)w * 1000 + r;
        RtThread *t = rt_thread_attach(NULL);
        if (t == NULL || rt_thread_self() != t) { c12.bad[w]++; continue; }
        if (rt_guest_call(&t->ctx, T_GUEST_ADDR(add_one), 1, &a) == a + 1) c12.ok[w]++;
        else c12.bad[w]++;
        rt_thread_detach(t);
    }
    return NULL;
}

TEST(core, thread_attach)
{
    T_PLAN(1, "attach makes the calling host thread a guest thread") {
        pthread_t p;
        memset(&c1, 0, sizeof c1);
        T_EQ(pthread_create(&p, NULL, case1_host, NULL), 0);
        T_EQ(pthread_join(p, NULL), 0);
        T_TRUE(c1.self_before == NULL);
        T_TRUE(c1.t != NULL);
        T_TRUE(c1.self_after == c1.t);
        T_TRUE(c1.name_ok);
        T_TRUE(c1.id % 4 == 0 && c1.id > RT_FIRST_THREAD_ID);
        T_EQ(c1.teb_self, c1.teb);
        T_EQ(c1.teb_id, c1.id);
        T_EQ(c1.stack_span, 0x100000);
        T_EQ(c1.count, 2);
        T_EQ(c1.kind, RT_TK_ATTACHED);
        T_EQ(c1.obj_kind, RT_K_THREAD);
    }

    T_PLAN(2, "guest code runs on the attached thread") {
        pthread_t p;
        memset(&c2, 0, sizeof c2);
        T_EQ(pthread_create(&p, NULL, case2_host, NULL), 0);
        T_EQ(pthread_join(p, NULL), 0);
        T_EQ(c2.got, c2.arg + 1);
    }

    T_PLAN(3, "a second attach returns the same thread; the name stays") {
        pthread_t p;
        memset(&c3, 0, sizeof c3);
        T_EQ(pthread_create(&p, NULL, case3_host, NULL), 0);
        T_EQ(pthread_join(p, NULL), 0);
        T_TRUE(c3.first != NULL);
        T_TRUE(c3.second == c3.first);
        T_TRUE(c3.name_ok);
        T_EQ(c3.count, 2);
    }

    T_PLAN(4, "detach ends the thread; its handle outlives it and shows the exit") {
        Ctx *ctx = T_CTX();
        pthread_t p;
        memset(&c4, 0, sizeof c4);
        T_EQ(pthread_create(&p, NULL, case4_host, NULL), 0);
        T_EQ(pthread_join(p, NULL), 0);
        T_TRUE(c4.self_after == NULL);
        T_EQ(c4.count, 1);
        T_TRUE(c4.hp != NULL);
        if (c4.hp) {
            T_EQ(rt_wait_handle(ctx, c4.hp, 0, FALSE), WAIT_OBJECT_0);
            T_EQ(object_of(c4.hp)->exit_code, 0);
            T_EQ(object_of(c4.hp)->thread->teb, 0);
        }
    }

    T_PLAN(5, "a NULL name leaves the name empty") {
        pthread_t p;
        memset(&c5, 0, sizeof c5);
        T_EQ(pthread_create(&p, NULL, case5_host, NULL), 0);
        T_EQ(pthread_join(p, NULL), 0);
        T_TRUE(c5.t != NULL);
        T_EQ(c5.first, 0);
    }

    T_PLAN(6, "attach on a guest thread returns that thread; its name is unchanged") {
        Ctx *ctx = T_CTX();
        char before[sizeof RT_THREAD(ctx)->name];
        memcpy(before, RT_THREAD(ctx)->name, sizeof before);
        T_TRUE(rt_thread_attach("x") == RT_THREAD(ctx));
        T_TRUE(memcmp(before, RT_THREAD(ctx)->name, sizeof before) == 0);
    }

    T_PLAN(7, "the exit path abandons a mutex the thread still owns") {
        Ctx *ctx = T_CTX();
        pthread_t p;
        memset(&c7, 0, sizeof c7);
        T_EQ(pthread_create(&p, NULL, case7_host, NULL), 0);
        T_EQ(pthread_join(p, NULL), 0);
        T_TRUE(c7.m != NULL);
        if (c7.m) T_EQ(rt_wait_handle(ctx, c7.m, 0, FALSE), WAIT_ABANDONED_0);
    }

    T_PLAN(8, "detach refuses NULL and a thread that is not an attached one") {
        T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
        T_TRAPS(rt_thread_detach(RT_THREAD(T_CTX())), "was not made by rt_thread_attach");
        T_TRAPS(rt_thread_detach(NULL), "not the calling thread's own RtThread");
    }

    T_PLAN(9, "detach from another host thread is refused; the owner detaches later") {
        pthread_t p;
        T_EQ(pthread_create(&p, NULL, case9_host, NULL), 0);
        pthread_mutex_lock(&c9.lock);
        while (!c9.attached) pthread_cond_wait(&c9.cv, &c9.lock);
        pthread_mutex_unlock(&c9.lock);
        T_TRUE(c9.t != NULL);
        T_TRAPS(rt_thread_detach(c9.t), "not the calling thread's own RtThread");
        pthread_mutex_lock(&c9.lock);
        c9.released = 1;
        pthread_cond_broadcast(&c9.cv);
        pthread_mutex_unlock(&c9.lock);
        T_EQ(pthread_join(p, NULL), 0);
    }

    T_PLAN(10, "rt_thread_exit on an attached thread is refused") {
        T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
        T_TRAPS(f10(), "a host thread the runtime did not create");
    }

    T_PLAN(11, "detach from inside a shim is refused") {
        T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
        T_TRAPS(g11(), "called from inside a shim");
    }

    T_PLAN(12, "eight host threads, 200 rounds each, all results right") {
        pthread_t p[WORKERS];
        memset(&c12, 0, sizeof c12);
        for (unsigned w = 0; w < WORKERS; w++) {
            T_EQ(pthread_create(&p[w], NULL, case12_host, (void *)(uintptr_t)w), 0);
        }
        for (unsigned w = 0; w < WORKERS; w++) {
            T_EQ(pthread_join(p[w], NULL), 0);
        }
        for (unsigned w = 0; w < WORKERS; w++) {
            T_EQ(c12.ok[w], ROUNDS);
            T_EQ(c12.bad[w], 0);
        }
        T_EQ(rt_thread_count(), 1);
    }
}
