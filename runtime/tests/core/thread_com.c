/* The COM counts of a thread (core/thread_com.c; the specification is rt/thread.h, "the COM
 * state of a thread").
 *
 * Where the expected values come from: the return values listed on Microsoft's pages of
 * CoInitializeEx and OleInitialize, and for the OLE count the sequences of Wine's
 * ole32/tests/compobj.c (test_CoInitializeEx, test_OleInitialize_InitCounting), replayed here
 * call by call with the results those tests require from Windows.  OleUninitialize does not
 * exist in this runtime; where a Wine sequence calls it to give back the initialisation that
 * OleInitialize holds, this test calls rt_com_uninitialize, which gives back the same count.
 * The numbers: S_OK 0, S_FALSE 1, RPC_E_CHANGED_MODE 0x80010106; COINIT_MULTITHREADED 0,
 * COINIT_APARTMENTTHREADED 2, COINIT_DISABLE_OLE1DDE 4, COINIT_SPEED_OVER_MEMORY 8.
 */
#include <pthread.h>

#include "rt/test.h"

#define SENTINEL 0x5EA1u
#define CHANGED_MODE ((HRESULT)0x80010106)

TEST(core, thread_com)
{
    Ctx *ctx = T_CTX();
    rt_set_last_error(ctx, SENTINEL);

    T_CASE("first S_OK, then S_FALSE for the same model, and the other model is RPC_E_CHANGED_MODE");
    T_EQ(rt_com_initialize(ctx, 2), 0);
    T_EQ(rt_com_initialize(ctx, 2), 1);
    T_EQ(rt_com_initialize(ctx, 2 | 4), 1);             /* the two other flags do not make another model */
    T_EQ(rt_com_initialize(ctx, 2 | 8), 1);
    T_EQ(rt_com_initialize(ctx, 0), CHANGED_MODE);
    T_EQ(rt_com_initialize(ctx, 0 | 4 | 8), CHANGED_MODE);

    T_CASE("every S_OK and S_FALSE is given back by one uninitialise; a failure is not counted");
    rt_com_uninitialize(ctx);                           /* four successes: three leave it initialised */
    rt_com_uninitialize(ctx);
    rt_com_uninitialize(ctx);
    T_EQ(rt_com_initialize(ctx, 0), CHANGED_MODE);      /* still an apartment thread */
    T_EQ(rt_com_initialize(ctx, 2), 1);
    rt_com_uninitialize(ctx);
    rt_com_uninitialize(ctx);                           /* the count is 0 now */

    T_CASE("after the last uninitialise the thread may take the other model");
    T_EQ(rt_com_initialize(ctx, 0), 0);
    T_EQ(rt_com_initialize(ctx, 0), 1);
    T_EQ(rt_com_initialize(ctx, 2), CHANGED_MODE);
    rt_com_uninitialize(ctx);
    rt_com_uninitialize(ctx);
    T_EQ(rt_com_initialize(ctx, 2), 0);
    rt_com_uninitialize(ctx);

    T_CASE("an uninitialise without an initialisation is logged and changes nothing");
    T_TRUE(!T_LOGGED(RT_CH_THREAD, "CoUninitialize on thread"));
    rt_com_uninitialize(ctx);
    T_TRUE(T_LOGGED(RT_CH_THREAD, "which has no COM initialisation to give back"));
    T_EQ(rt_com_initialize(ctx, 2), 0);                 /* the count did not go below 0 */
    rt_com_uninitialize(ctx);

    T_CASE("last error is never touched");
    T_EQ(T_LAST_ERROR(), SENTINEL);

    T_CASE("an unknown flag and a Ctx that is no thread's are fatal");
    T_TRAPS(rt_com_initialize(ctx, 1), "outside the four COINIT_ values");
    T_TRAPS(rt_com_initialize(ctx, 0x10), "outside the four COINIT_ values");
    T_TRAPS(rt_com_initialize(NULL, 2), "CoInitializeEx: the Ctx is not a guest thread's");
    T_TRAPS(rt_com_uninitialize(NULL), "CoUninitialize: the Ctx is not a guest thread's");
    T_TRAPS(rt_ole_initialize(NULL), "OleInitialize: the Ctx is not a guest thread's");
}

/* Each sequence runs on a thread of its own, because the OLE count of a thread never goes down. */
static HRESULT seq[4][8];
static int seq_ran[4];

static void *ole_sequence(void *arg)
{
    const unsigned n = (unsigned)(uintptr_t)arg;
    RtThread *t = rt_thread_attach("ole");
    if (!t) return NULL;
    Ctx *c = &t->ctx;
    HRESULT *r = seq[n];
    switch (n) {
    case 0:     /* Wine test_CoInitializeEx */
        r[0] = rt_com_initialize(c, 2);
        r[1] = rt_ole_initialize(c);
        r[2] = rt_ole_initialize(c);
        rt_com_uninitialize(c); rt_com_uninitialize(c); rt_com_uninitialize(c);
        r[3] = rt_com_initialize(c, 0);                 /* all three were given back */
        break;
    case 1:     /* Wine test_OleInitialize_InitCounting, part 1 */
        r[0] = rt_com_initialize(c, 0);
        r[1] = rt_ole_initialize(c);
        rt_com_uninitialize(c);
        r[2] = rt_ole_initialize(c);                    /* the failure counted nothing: a first OleInitialize */
        break;
    case 2:     /* Wine test_OleInitialize_InitCounting, part 3 */
        r[0] = rt_com_initialize(c, 2);
        r[1] = rt_ole_initialize(c);
        rt_com_uninitialize(c); rt_com_uninitialize(c);
        r[2] = rt_ole_initialize(c);
        rt_com_uninitialize(c);
        r[3] = rt_com_initialize(c, 0);                 /* that OleInitialize held one initialisation */
        break;
    default:    /* OleInitialize on a thread that never initialised COM */
        r[0] = rt_ole_initialize(c);
        r[1] = rt_com_initialize(c, 2);
        r[2] = rt_com_initialize(c, 0);
        break;
    }
    seq_ran[n] = 1;
    rt_thread_detach(t);
    return NULL;
}

TEST(core, thread_ole)
{
    pthread_t th[4];
    for (uintptr_t i = 0; i < 4; i++) T_REQUIRE(pthread_create(&th[i], NULL, ole_sequence, (void *)i) == 0);
    for (unsigned i = 0; i < 4; i++) pthread_join(th[i], NULL);
    for (unsigned i = 0; i < 4; i++) T_REQUIRE(seq_ran[i]);

    T_CASE("the first OleInitialize is S_OK on a thread CoInitializeEx initialised, the second S_FALSE");
    T_EQ(seq[0][0], 0);
    T_EQ(seq[0][1], 0);
    T_EQ(seq[0][2], 1);
    T_EQ(seq[0][3], 0);

    T_CASE("OleInitialize on a multithreaded thread is RPC_E_CHANGED_MODE and counts nothing");
    T_EQ(seq[1][0], 0);
    T_EQ(seq[1][1], CHANGED_MODE);
    T_EQ(seq[1][2], 0);

    T_CASE("the OLE count outlives CoUninitialize: a later OleInitialize is S_FALSE");
    T_EQ(seq[2][0], 0);
    T_EQ(seq[2][1], 0);
    T_EQ(seq[2][2], 1);
    T_EQ(seq[2][3], 0);

    T_CASE("OleInitialize alone makes the thread an apartment thread");
    T_EQ(seq[3][0], 0);
    T_EQ(seq[3][1], 1);
    T_EQ(seq[3][2], CHANGED_MODE);

    T_CASE("the counts are per thread: the main thread is still uninitialised");
    T_EQ(rt_com_initialize(T_CTX(), 0), 0);
    rt_com_uninitialize(T_CTX());
}
