/* Alertable waits, asynchronous procedure calls and SleepEx (docs/RUNTIME.md sections 4.2 and
 * 6.3; rt/sync.h: rt_apc_queue, rt_sleep_ex).
 *
 * An APC is a guest function, so it can only be delivered on a real guest thread: every
 * alertable wait here is made by the test's main thread (guest thread 1), and the worker
 * threads only queue.  Helpers: wait_util.c.
 */
#include "wait_util.c"

#define CTX        T_CTX()
#define ME         RT_THREAD(CTX)
#define SENTINEL   0xABCD1234u

static uint64_t apc_log[64];
static unsigned apc_n;
static int      apc_saw_lock;           /* an APC ran with the dispatcher lock held */
static int      apc_saw_host_mode;      /* an APC ran marked as inside a shim */
static DWORD    nested_result;
static HANDLE   nested_handle;

static void note(Ctx *ctx) {
    if (apc_n < 64) apc_log[apc_n] = ctx->r[1];
    apc_n++;
    if (rt_dispatcher_held()) apc_saw_lock = 1;
    if (RT_THREAD(ctx)->in_host) apc_saw_host_mode = 1;
}

/* VOID CALLBACK apc_record(ULONG_PTR param) */
GUEST_FN(apc_record)
{
    note(ctx);
    GUEST_RET(ctx);
}

/* Queues its successor to its own thread until the parameter reaches 5. */
GUEST_FN(apc_chain)
{
    note(ctx);
    uint64_t p = ctx->r[1];
    if (p < 5) rt_apc_queue(ctx, RT_THREAD(ctx), T_GUEST_ADDR(apc_chain), p + 1);
    GUEST_RET(ctx);
}

/* Waits alertably itself: that wait runs whatever else is queued. */
GUEST_FN(apc_nested_alertable)
{
    note(ctx);
    nested_result = rt_sleep_ex(ctx, 0, TRUE);
    GUEST_RET(ctx);
}

/* Waits, not alertably: nothing else runs inside it. */
GUEST_FN(apc_nested_plain)
{
    note(ctx);
    unsigned before = apc_n;
    nested_result = rt_wait_handle(ctx, nested_handle, 5, FALSE);
    if (apc_n != before) apc_saw_lock = 2;              /* reported as a failure below */
    GUEST_RET(ctx);
}

static BOOL queue_me(GUESTFN fn, uint64_t param) { return rt_apc_queue(CTX, ME, fn, param); }
static void reset_log(void) { apc_n = 0; memset(apc_log, 0, sizeof apc_log); }

/* ---- only an alertable wait runs them ----------------------------------------------------------- */

TEST(core, wait_apc_only_alertable_waits_run_them)
{
    HANDLE e = rt_event_create(CTX, FALSE, FALSE, NULL), e2 = rt_event_create(CTX, FALSE, FALSE, NULL);
    HANDLE both[2] = { e, e2 };

    T_EQ(queue_me(T_GUEST_ADDR(apc_record), 0x11), TRUE);
    T_EQ(queue_me(T_GUEST_ADDR(apc_record), 0x1122334455667788), TRUE);
    T_EQ(queue_me(T_GUEST_ADDR(apc_record), 0), TRUE);
    T_EQ(apc_n, 0);                                     /* queueing calls nothing */

    /* Waits that are not alertable never run an APC, whether they poll, sleep or time out. */
    T_EQ(rt_wait_handle(CTX, e, 0, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_wait_handle(CTX, e, 15, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_wait_handles(CTX, 2, both, FALSE, 5, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_wait_handles(CTX, 2, both, TRUE, 5, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_signal_and_wait(CTX, e2, e, 5, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_wait_handle(CTX, e2, 0, FALSE), WAIT_OBJECT_0);     /* (the signal half did happen) */
    T_EQ(rt_sleep_ex(CTX, 0, FALSE), 0);
    T_EQ(rt_sleep_ex(CTX, 10, FALSE), 0);
    T_EQ(apc_n, 0);

    /* An alertable one runs all of them, oldest first, each once, and says so, even though
     * its timeout is zero.  Nothing was taken and nothing is left queued. */
    const uint64_t rsp = CTX->r[4];
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait_handle(CTX, e, 0, TRUE), WAIT_IO_COMPLETION);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_EQ(apc_n, 3);
    T_EQ(apc_log[0], 0x11);
    T_EQ(apc_log[1], 0x1122334455667788);               /* the parameter is 64 bits wide */
    T_EQ(apc_log[2], 0);
    T_EQ(CTX->r[4], rsp);                               /* the guest stack pointer is back */
    T_EQ(apc_saw_lock, 0);                              /* no lock is held across the guest call */
    T_EQ(apc_saw_host_mode, 0);                         /* and the call is entered as guest code */
    T_EQ(rt_dispatcher_held(), false);
    T_EQ(wu_queued(e), 0);

    T_EQ(rt_wait_handle(CTX, e, 0, TRUE), WAIT_TIMEOUT);        /* the queue is empty again */
    T_EQ(rt_wait_handle(CTX, e, 10, TRUE), WAIT_TIMEOUT);
    T_EQ(apc_n, 3);

    /* Each alertable entry point. */
    reset_log();
    queue_me(T_GUEST_ADDR(apc_record), 1);
    T_EQ(rt_wait_handles(CTX, 2, both, FALSE, INFINITE, TRUE), WAIT_IO_COMPLETION);
    queue_me(T_GUEST_ADDR(apc_record), 2);
    T_EQ(rt_wait_handles(CTX, 2, both, TRUE, 0, TRUE), WAIT_IO_COMPLETION);
    queue_me(T_GUEST_ADDR(apc_record), 3);
    T_EQ(rt_signal_and_wait(CTX, e2, e, INFINITE, TRUE), WAIT_IO_COMPLETION);
    T_EQ(wu_state(e2).set, true);                       /* signalled, and then the wait was alerted */
    queue_me(T_GUEST_ADDR(apc_record), 4);
    T_EQ(rt_sleep_ex(CTX, INFINITE, TRUE), WAIT_IO_COMPLETION);
    queue_me(T_GUEST_ADDR(apc_record), 5);
    T_EQ(rt_sleep_ex(CTX, 0, TRUE), WAIT_IO_COMPLETION);
    queue_me(T_GUEST_ADDR(apc_record), 6);
    RtObject *o = wu_obj(e);
    T_EQ(rt_wait(CTX, 1, &o, false, 1000, true), WAIT_IO_COMPLETION);
    T_EQ(apc_n, 6);
    for (unsigned i = 0; i < 6; i++) T_EQ(apc_log[i], i + 1);
}

/* ---- a signalled object wins over a queued APC --------------------------------------------------- */

TEST(core, wait_apc_signalled_object_wins)
{
    HANDLE e = rt_event_create(CTX, FALSE, TRUE, NULL);         /* auto-reset, set */
    HANDLE unset = rt_event_create(CTX, FALSE, FALSE, NULL);
    HANDLE manual = rt_event_create(CTX, TRUE, TRUE, NULL);

    T_EQ(queue_me(T_GUEST_ADDR(apc_record), 7), TRUE);
    T_EQ(rt_wait_handle(CTX, e, 0, TRUE), WAIT_OBJECT_0);       /* the object, not the APC */
    T_EQ(apc_n, 0);
    T_EQ(wu_state(e).set, false);
    HANDLE any[2] = { unset, manual };
    T_EQ(rt_wait_handles(CTX, 2, any, FALSE, INFINITE, TRUE), WAIT_OBJECT_0 + 1);
    HANDLE all[2] = { manual, e };
    rt_event_set(CTX, e);
    T_EQ(rt_wait_handles(CTX, 2, all, TRUE, INFINITE, TRUE), WAIT_OBJECT_0);
    T_EQ(apc_n, 0);                                     /* still queued */

    /* Nothing signalled: now the APC ends the wait, and the wait takes nothing. */
    rt_event_set(CTX, e);
    HANDLE part[2] = { e, unset };                      /* a wait-all that is half satisfied */
    T_EQ(rt_wait_handles(CTX, 2, part, TRUE, INFINITE, TRUE), WAIT_IO_COMPLETION);
    T_EQ(apc_n, 1);
    T_EQ(apc_log[0], 7);
    T_EQ(wu_state(e).set, true);
    T_EQ(wu_queued(e), 0);
    T_EQ(wu_queued(unset), 0);
}

/* ---- APCs queued while APCs run ------------------------------------------------------------------- */

TEST(core, wait_apc_queued_while_running)
{
    /* One APC that queues the next, five deep: all five run inside the one wait. */
    T_EQ(queue_me(T_GUEST_ADDR(apc_chain), 1), TRUE);
    T_EQ(rt_sleep_ex(CTX, 0, TRUE), WAIT_IO_COMPLETION);
    T_EQ(apc_n, 5);
    for (unsigned i = 0; i < 5; i++) T_EQ(apc_log[i], i + 1);
    T_EQ(rt_sleep_ex(CTX, 0, TRUE), 0);

    /* An APC that waits alertably: its wait runs the rest of the queue and reports it; the
     * outer wait then finds nothing left and still reports its own APC. */
    reset_log();
    queue_me(T_GUEST_ADDR(apc_nested_alertable), 100);
    queue_me(T_GUEST_ADDR(apc_record), 101);
    queue_me(T_GUEST_ADDR(apc_record), 102);
    nested_result = 0xDEAD;
    T_EQ(rt_sleep_ex(CTX, 0, TRUE), WAIT_IO_COMPLETION);
    T_EQ(nested_result, WAIT_IO_COMPLETION);
    T_EQ(apc_n, 3);
    T_EQ(apc_log[0], 100);
    T_EQ(apc_log[1], 101);
    T_EQ(apc_log[2], 102);

    /* An APC that waits without being alertable: the others wait for it to return. */
    reset_log();
    nested_handle = rt_event_create(CTX, FALSE, FALSE, NULL);
    queue_me(T_GUEST_ADDR(apc_nested_plain), 200);
    queue_me(T_GUEST_ADDR(apc_record), 201);
    nested_result = 0xDEAD;
    T_EQ(rt_wait_handle(CTX, nested_handle, 0, TRUE), WAIT_IO_COMPLETION);
    T_EQ(nested_result, WAIT_TIMEOUT);
    T_EQ(apc_n, 2);
    T_EQ(apc_log[0], 200);
    T_EQ(apc_log[1], 201);
    T_EQ(apc_saw_lock, 0);                              /* 2 would mean an APC ran inside the plain wait */
    T_EQ(wu_queued(nested_handle), 0);
}

/* ---- an APC queued by another thread wakes a sleeping alertable wait -------------------------- */

typedef struct {
    HANDLE    watch;                /* queue the APC once the target is queued on this (NULL: after a moment) */
    RtThread *target;
    uint64_t  param;
    bool      no_ctx;
    HANDLE    then_set;             /* if not NULL: afterwards make sure the wait went on, and set this */
} Send;

static void send_apc(Worker *w) {
    Send *s = w->arg;
    if (s->watch) W_CHECK(w, EVENTUALLY(wu_queued(s->watch) == 1));
    else usleep(30000);
    W_CHECK(w, rt_apc_queue(s->no_ctx ? NULL : W_CTX(w), s->target, T_GUEST_ADDR(apc_record), s->param) == TRUE);
    if (s->then_set) {
        wu_settle();
        W_CHECK(w, wu_queued(s->watch) == 1);           /* the plain wait was not disturbed */
        W_CHECK(w, rt_event_set(W_CTX(w), s->then_set) == TRUE);
    }
}

TEST(core, wait_apc_wakes_a_sleeping_alertable_wait)
{
    static Worker w;
    HANDLE e = rt_event_create(CTX, FALSE, FALSE, NULL), set = rt_event_create(CTX, FALSE, TRUE, NULL);
    static const DWORD timeouts[] = { INFINITE, 0xFFFFFFFEu, 600000 };

    /* A wait on one object, with and without a timeout. */
    for (unsigned i = 0; i < 3; i++) {
        Send s = { e, ME, 0x40 + i, i == 1, NULL };
        reset_log();
        wu_start_one(&w, 0, send_apc, &s);
        T_EQ(rt_wait_handle(CTX, e, timeouts[i], TRUE), WAIT_IO_COMPLETION);
        T_EQ(apc_n, 1);
        T_EQ(apc_log[0], 0x40 + i);
        T_EQ(wu_queued(e), 0);                          /* the thread left the queue */
        T_EQ(wu_state(e).set, false);
        if (!wu_finish(&w, 1, "APC sender")) return;
    }

    /* A wait-all that is half satisfied: alerted, it takes nothing. */
    HANDLE both[2] = { set, e };
    Send s = { e, ME, 0x50, false, NULL };
    reset_log();
    wu_start_one(&w, 0, send_apc, &s);
    T_EQ(rt_wait_handles(CTX, 2, both, TRUE, INFINITE, TRUE), WAIT_IO_COMPLETION);
    T_EQ(apc_n, 1);
    T_EQ(wu_state(set).set, true);
    T_EQ(wu_queued(set), 0);
    T_EQ(wu_queued(e), 0);
    if (!wu_finish(&w, 1, "APC sender")) return;

    /* A sleep. */
    Send t = { NULL, ME, 0x60, false, NULL };
    reset_log();
    wu_start_one(&w, 0, send_apc, &t);
    T_EQ(rt_sleep_ex(CTX, INFINITE, TRUE), WAIT_IO_COMPLETION);
    T_EQ(apc_n, 1);
    T_EQ(apc_log[0], 0x60);
    if (!wu_finish(&w, 1, "APC sender")) return;
    T_EQ(apc_saw_lock, 0);
}

TEST(core, wait_apc_does_not_wake_a_plain_wait)
{
    static Worker w;
    HANDLE e = rt_event_create(CTX, FALSE, FALSE, NULL);
    Send s = { e, ME, 0x70, false, e };
    wu_start_one(&w, 0, send_apc, &s);
    /* Not alertable: the APC arrives while this thread sleeps and it sleeps on, until the
     * event is set. */
    T_EQ(rt_wait_handle(CTX, e, INFINITE, FALSE), WAIT_OBJECT_0);
    T_EQ(apc_n, 0);
    if (!wu_finish(&w, 1, "APC sender")) return;
    T_EQ(rt_sleep_ex(CTX, 5, FALSE), 0);
    T_EQ(apc_n, 0);
    T_EQ(rt_sleep_ex(CTX, 5, TRUE), WAIT_IO_COMPLETION);        /* the next alertable wait gets it */
    T_EQ(apc_n, 1);
    T_EQ(apc_log[0], 0x70);
}

/* ---- threads that never run them, and failures ---------------------------------------------------- */

TEST(core, wait_apc_target_rules)
{
    /* A thread that has never waited can be given APCs: its wait record is made for it. */
    static Worker w;
    wu_make_thread(&w, 0);
    T_TRUE(w.t.sub[RT_SUB_WAIT] == NULL);
    T_EQ(rt_apc_queue(CTX, &w.t, T_GUEST_ADDR(apc_record), 1), TRUE);
    T_TRUE(w.t.sub[RT_SUB_WAIT] != NULL);
    T_EQ(rt_apc_queue(NULL, &w.t, T_GUEST_ADDR(apc_record), 2), TRUE);

    /* It exits without ever waiting alertably: they are dropped, never run. */
    rt_wait_thread_exit(&w.t);
    T_TRUE(T_LOGGED(RT_CH_THREAD, "exited with 2 APCs still queued: dropped"));
    T_EQ(apc_n, 0);

    /* Queueing to a thread that has exited fails. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_apc_queue(CTX, &w.t, T_GUEST_ADDR(apc_record), 3), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_GEN_FAILURE);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_apc_queue(NULL, &w.t, T_GUEST_ADDR(apc_record), 3), FALSE);     /* no ctx: the thread's own last error */
    T_EQ(rt_thread_get_last_error(), ERROR_GEN_FAILURE);

    /* Success leaves last error alone. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(queue_me(T_GUEST_ADDR(apc_record), 9), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_EQ(rt_sleep_ex(CTX, 0, TRUE), WAIT_IO_COMPLETION);
    T_EQ(apc_n, 1);

    /* One APC: the singular in the log line of the exit. */
    static Worker v;
    wu_make_thread(&v, 1);
    T_EQ(rt_apc_queue(CTX, &v.t, T_GUEST_ADDR(apc_record), 1), TRUE);
    rt_wait_thread_exit(&v.t);
    T_TRUE(T_LOGGED(RT_CH_THREAD, "exited with 1 APC still queued: dropped"));

    T_TRAPS(rt_apc_queue(CTX, NULL, T_GUEST_ADDR(apc_record), 0), "rt_apc_queue: NULL target thread");
    /* A NULL function is queued like any other and fails where it is called. */
    T_TRAPS((queue_me(0, 0), rt_sleep_ex(CTX, 0, TRUE)), "null function pointer");
}

/* ---- an APC of several arguments (rt_apc_queue_args: the completion routine of ReadFileEx) -------- */

static uint64_t apc3_log[4][3];
static unsigned apc3_n;

/* VOID CALLBACK apc_record3(a, b, c): the three arguments as they arrive, in rcx, rdx and r8. */
GUEST_FN(apc_record3)
{
    if (apc3_n < 4) {
        apc3_log[apc3_n][0] = ctx->r[1];
        apc3_log[apc3_n][1] = ctx->r[2];
        apc3_log[apc3_n][2] = ctx->r[8];
    }
    apc3_n++;
    note(ctx);
    GUEST_RET(ctx);
}

TEST(core, wait_apc_three_arguments)
{
    static const uint64_t a3[3] = { 0x1111111122222222, 0xFFFFFFFF00000001, 0x8000000000000003 };
    uint64_t two[2] = { 7, 8 }, one = 5;

    /* One queue for both forms: they run in the order queued, each once, and only in an
     * alertable wait. */
    T_EQ(queue_me(T_GUEST_ADDR(apc_record), 1), TRUE);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_apc_queue_args(CTX, ME, T_GUEST_ADDR(apc_record3), 3, a3), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_EQ(rt_apc_queue_args(CTX, ME, T_GUEST_ADDR(apc_record3), 2, two), TRUE);
    two[0] = two[1] = 0xBAD;                            /* the arguments were copied when queued */
    T_EQ(queue_me(T_GUEST_ADDR(apc_record), 4), TRUE);
    T_EQ(rt_apc_queue_args(NULL, ME, T_GUEST_ADDR(apc_record), 1, &one), TRUE);
    T_EQ(apc_n, 0);                                     /* queueing calls nothing */
    T_EQ(rt_sleep_ex(CTX, 0, FALSE), 0);
    T_EQ(rt_sleep_ex(CTX, 5, FALSE), 0);
    T_EQ(apc_n, 0);

    const uint64_t rsp = CTX->r[4];
    T_EQ(rt_sleep_ex(CTX, 0, TRUE), WAIT_IO_COMPLETION);
    T_EQ(CTX->r[4], rsp);
    T_EQ(apc_n, 5);
    T_EQ(apc_log[0], 1);
    T_EQ(apc_log[1], a3[0]);
    T_EQ(apc_log[2], 7);
    T_EQ(apc_log[3], 4);
    T_EQ(apc_log[4], 5);
    T_EQ(apc3_n, 2);
    T_EQ(apc3_log[0][0], a3[0]);                        /* each argument is 64 bits wide */
    T_EQ(apc3_log[0][1], a3[1]);
    T_EQ(apc3_log[0][2], a3[2]);
    T_EQ(apc3_log[1][0], 7);
    T_EQ(apc3_log[1][1], 8);
    T_EQ(apc_saw_lock, 0);
    T_EQ(apc_saw_host_mode, 0);
    T_EQ(rt_sleep_ex(CTX, 0, TRUE), 0);                 /* nothing is left: none runs twice */
    T_EQ(apc_n, 5);

    /* The target's rules are rt_apc_queue's: dropped at exit, refused afterwards. */
    static Worker w;
    wu_make_thread(&w, 0);
    T_EQ(rt_apc_queue_args(CTX, &w.t, T_GUEST_ADDR(apc_record3), 3, a3), TRUE);
    T_EQ(rt_apc_queue(CTX, &w.t, T_GUEST_ADDR(apc_record), 2), TRUE);
    rt_wait_thread_exit(&w.t);
    T_TRUE(T_LOGGED(RT_CH_THREAD, "exited with 2 APCs still queued: dropped"));
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_apc_queue_args(CTX, &w.t, T_GUEST_ADDR(apc_record3), 3, a3), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_GEN_FAILURE);
    T_EQ(apc_n, 5);

    T_TRAPS(rt_apc_queue_args(CTX, ME, T_GUEST_ADDR(apc_record3), 0, a3), "rt_apc_queue_args: 0 arguments");
    T_TRAPS(rt_apc_queue_args(CTX, ME, T_GUEST_ADDR(apc_record3), 4, a3), "rt_apc_queue_args: 4 arguments");
    T_TRAPS(rt_apc_queue_args(CTX, ME, T_GUEST_ADDR(apc_record3), 3, NULL), "rt_apc_queue_args: 3 arguments");
    T_TRAPS(rt_apc_queue_args(CTX, NULL, T_GUEST_ADDR(apc_record3), 3, a3), "rt_apc_queue: NULL target thread");
}

/* ---- SleepEx ----------------------------------------------------------------------------------------- */

static void sleeper(Worker *w) {
    uint64_t t0 = rt_time_mono_100ns();
    w->result = rt_sleep_ex(W_CTX(w), 30, FALSE);
    w->n[0] = rt_time_mono_100ns() - t0;
}

TEST(core, wait_sleep)
{
    /* Zero gives up the processor and returns 0, alertable or not. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_sleep_ex(CTX, 0, FALSE), 0);
    T_EQ(rt_sleep_ex(CTX, 0, TRUE), 0);

    /* A sleep lasts at least as long as asked, on the runtime's clock, and returns 0 (not
     * WAIT_TIMEOUT, which is what the wait core calls it). */
    static const DWORD ms[] = { 1, 3, 25 };
    for (unsigned i = 0; i < 3; i++) {
        for (int alertable = 0; alertable < 2; alertable++) {
            uint64_t t0 = rt_time_mono_100ns();
            T_EQ(rt_sleep_ex(CTX, ms[i], alertable), 0);
            T_TRUE(rt_time_mono_100ns() - t0 >= (uint64_t)ms[i] * 10000);
        }
    }
    T_EQ(T_LAST_ERROR(), SENTINEL);                     /* sleeping is not an error */
    T_EQ(rt_dispatcher_held(), false);

    /* An APC already queued ends an alertable sleep before it starts, whatever its length. */
    queue_me(T_GUEST_ADDR(apc_record), 1);
    uint64_t t0 = wu_now_ns();
    T_EQ(rt_sleep_ex(CTX, 3600000, TRUE), WAIT_IO_COMPLETION);
    T_TRUE(wu_now_ns() - t0 < UINT64_C(60000000000));
    T_EQ(apc_n, 1);

    /* Threads other than this one: eight sleep 30 ms each, not alertably, and all come back. */
    static Worker w[8];
    wu_start(w, 8, sleeper, NULL);
    if (!wu_finish(w, 8, "sleepers")) return;
    for (unsigned i = 0; i < 8; i++) {
        T_EQ(w[i].result, 0);
        T_TRUE(w[i].n[0] >= 30 * 10000);
    }
}
