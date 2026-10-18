/* The error mode, the process times and the execution state of a thread (core/process.c; the
 * specification is rt/thread.h, the last three functions of "the process").
 *
 * Where the expected values come from: the comments of rt/thread.h, which quote Microsoft's
 * pages of SetErrorMode, GetProcessTimes and SetThreadExecutionState.  The first value of the
 * execution state, ES_CONTINUOUS, is marked "from memory" there.  The numeric values of the
 * flags are written out so that a changed constant is seen.
 */
#include <pthread.h>
#include <sys/resource.h>

#include "rt/test.h"
#include "rt/sync.h"
#include "rt/time.h"

#define SENTINEL 0x5EA1u

static uint64_t u64_of(FILETIME ft) { return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime; }

/* ---- SetErrorMode ---------------------------------------------------------------------------- */

TEST(core, process_error_mode)
{
    Ctx *ctx = T_CTX();

    T_CASE("the mode starts at 0 and every call returns what the one before stored");
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_process_swap_error_mode(0x0001), 0);                    /* SEM_FAILCRITICALERRORS */
    T_EQ(rt_process_swap_error_mode(0x8003), 0x0001);               /* the three accepted bits */
    T_EQ(rt_process_swap_error_mode(0x0002), 0x8003);               /* SEM_NOGPFAULTERRORBOX */
    T_EQ(rt_process_swap_error_mode(0x8000), 0x0002);               /* SEM_NOOPENFILEERRORBOX */
    T_EQ(rt_process_swap_error_mode(0), 0x8000);
    T_EQ(rt_process_swap_error_mode(0), 0);
    T_EQ(T_LAST_ERROR(), SENTINEL);                                 /* never touched */

    T_CASE("the alignment flag and an unknown bit are fatal and change nothing");
    T_EQ(rt_process_swap_error_mode(0x0001), 0);
    T_TRAPS(rt_process_swap_error_mode(0x0004), "SEM_NOALIGNMENTFAULTEXCEPT");
    T_TRAPS(rt_process_swap_error_mode(0x0005), "SEM_NOALIGNMENTFAULTEXCEPT");
    T_TRAPS(rt_process_swap_error_mode(0x0008), "outside the four SEM_ values");
    T_TRAPS(rt_process_swap_error_mode(0x80000000u), "outside the four SEM_ values");
    T_EQ(rt_process_swap_error_mode(0), 0x0001);                    /* the traps ran in children */
}

/* Two threads swap at once: each value that goes in comes out exactly once. */
#define SWAPS 20000
static uint64_t swap_sum[2];

static void *swapper(void *arg)
{
    const unsigned me = (unsigned)(uintptr_t)arg;
    uint64_t got = 0;
    for (unsigned i = 0; i < SWAPS; i++)
        got += rt_process_swap_error_mode(me ? 0x0002 : 0x0001);
    swap_sum[me] = got;
    return NULL;
}

TEST(core, process_error_mode_threads)
{
    pthread_t th[2];

    T_CASE("a swap is atomic: no stored value is lost or seen twice");
    for (uintptr_t i = 0; i < 2; i++) T_REQUIRE(pthread_create(&th[i], NULL, swapper, (void *)i) == 0);
    for (unsigned i = 0; i < 2; i++) pthread_join(th[i], NULL);
    /* SWAPS ones and SWAPS twos went in after the 0 at the start; all but the last came out. */
    const UINT last = rt_process_swap_error_mode(0);
    T_TRUE(last == 0x0001 || last == 0x0002);
    T_EQ(swap_sum[0] + swap_sum[1] + last, (uint64_t)SWAPS * 3);
}

/* ---- GetProcessTimes ------------------------------------------------------------------------- */

/* Use the processor for a while, so that the user time moves by more than the host's unit. */
static volatile uint64_t burn_sink;
static void burn(void)
{
    struct rusage a, b;
    getrusage(RUSAGE_SELF, &a);
    do {
        for (unsigned i = 0; i < 2000000; i++) burn_sink += i * 2654435761u;
        getrusage(RUSAGE_SELF, &b);
    } while ((b.ru_utime.tv_sec - a.ru_utime.tv_sec) * 1000000 + (b.ru_utime.tv_usec - a.ru_utime.tv_usec) < 20000);
}

TEST(core, process_times)
{
    Ctx *ctx = T_CTX();
    FILETIME c = { 1, 1 }, k = { 1, 1 }, u = { 1, 1 }, c2, k2, u2;

    T_CASE("the creation time lies between the test's start and now, and is the same on every call");
    const uint64_t now = rt_time_utc_100ns();
    rt_set_last_error(ctx, SENTINEL);
    T_TRUE(rt_process_times(RT_PSEUDO_PROCESS, &c, &k, &u));
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_TRUE(u64_of(c) <= now);
    T_TRUE(now - u64_of(c) < UINT64_C(300) * 10000000);             /* a test has 300 s */
    T_TRUE(u64_of(c) > UINT64_C(0x01D0000000000000));               /* after 2014: a FILETIME, not Unix time */

    T_CASE("kernel and user time are lengths in 100 ns units that only grow");
    struct rusage ru;
    T_REQUIRE(getrusage(RUSAGE_SELF, &ru) == 0);
    const uint64_t host_user = (uint64_t)ru.ru_utime.tv_sec * 10000000 + (uint64_t)ru.ru_utime.tv_usec * 10;
    T_TRUE(u64_of(u) <= host_user);                                 /* read before this getrusage */
    T_TRUE(u64_of(u) % 10 == 0 && u64_of(k) % 10 == 0);             /* the host counts microseconds */
    burn();
    T_TRUE(rt_process_times(RT_PSEUDO_PROCESS, &c2, &k2, &u2));
    T_EQ(u64_of(c2), u64_of(c));
    T_TRUE(u64_of(u2) >= u64_of(u) + 200000);                       /* burn used at least 20 ms */
    T_TRUE(u64_of(u2) - u64_of(u) < UINT64_C(300) * 10000000 * 64); /* and not more than every core for 300 s */
    T_TRUE(u64_of(k2) >= u64_of(k));

    T_CASE("a real handle to the process is accepted; another kind of handle is not");
    HANDLE dup = rt_handle_dup(RT_PSEUDO_PROCESS);
    T_REQUIRE(dup != NULL);
    T_TRUE(rt_process_times(dup, &c2, &k2, &u2));
    T_EQ(u64_of(c2), u64_of(c));
    T_TRUE(rt_handle_close(dup));

    HANDLE ev = rt_event_create(ctx, TRUE, FALSE, NULL);
    T_REQUIRE(ev != NULL);
    c2 = k2 = u2 = (FILETIME){ 0x11111111, 0x22222222 };
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_process_times(ev, &c2, &k2, &u2), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(u64_of(c2), UINT64_C(0x2222222211111111));                 /* nothing written */
    T_EQ(u64_of(k2), UINT64_C(0x2222222211111111));
    T_EQ(u64_of(u2), UINT64_C(0x2222222211111111));
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_process_times(NULL, &c2, &k2, &u2), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("a NULL pointer is fatal");
    T_TRAPS(rt_process_times(RT_PSEUDO_PROCESS, NULL, &k, &u), "a time pointer is NULL");
    T_TRAPS(rt_process_times(RT_PSEUDO_PROCESS, &c, NULL, &u), "a time pointer is NULL");
    T_TRAPS(rt_process_times(RT_PSEUDO_PROCESS, &c, &k, NULL), "a time pointer is NULL");
}

/* ---- SetThreadExecutionState ----------------------------------------------------------------- */

static struct { EXECUTION_STATE first, second, third; int attached; } other;

static void *other_thread(void *arg)
{
    (void)arg;
    RtThread *t = rt_thread_attach("exec-state");
    if (!t) return NULL;
    other.attached = 1;
    other.first = rt_thread_execution_state(&t->ctx, 0x80000001u);
    other.second = rt_thread_execution_state(&t->ctx, 0x80000000u);
    other.third = rt_thread_execution_state(&t->ctx, 0);
    rt_thread_detach(t);
    return NULL;
}

TEST(core, thread_execution_state)
{
    Ctx *ctx = T_CTX();

    T_CASE("a first call returns ES_CONTINUOUS; a call with ES_CONTINUOUS stores its flags");
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_thread_execution_state(ctx, 0x80000003u), 0x80000000u); /* first value: from memory */
    T_EQ(rt_thread_execution_state(ctx, 0x80000041u), 0x80000003u); /* away mode with CONTINUOUS */
    T_EQ(rt_thread_execution_state(ctx, 0x80000000u), 0x80000041u); /* clears the other flags */
    T_EQ(rt_thread_execution_state(ctx, 0x80000002u), 0x80000000u);
    T_EQ(T_LAST_ERROR(), SENTINEL);

    T_CASE("a call without ES_CONTINUOUS returns the state and leaves it as it is");
    T_EQ(rt_thread_execution_state(ctx, 0x00000001u), 0x80000002u); /* ES_SYSTEM_REQUIRED: a timer reset */
    T_EQ(rt_thread_execution_state(ctx, 0x00000003u), 0x80000002u);
    T_EQ(rt_thread_execution_state(ctx, 0), 0x80000002u);
    T_EQ(T_LAST_ERROR(), SENTINEL);

    T_CASE("ES_USER_PRESENT with another flag fails and records nothing");
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_thread_execution_state(ctx, 0x80000004u), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_thread_execution_state(ctx, 0x00000005u), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    rt_set_last_error(ctx, SENTINEL);
    T_EQ(rt_thread_execution_state(ctx, 0x80000047u), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(rt_thread_execution_state(ctx, 0), 0x80000002u);           /* unchanged */

    T_CASE("the state is per thread: another thread starts at ES_CONTINUOUS and does not change this one");
    pthread_t th;
    T_REQUIRE(pthread_create(&th, NULL, other_thread, NULL) == 0);
    pthread_join(th, NULL);
    T_REQUIRE(other.attached);
    T_EQ(other.first, 0x80000000u);
    T_EQ(other.second, 0x80000001u);
    T_EQ(other.third, 0x80000000u);
    T_EQ(rt_thread_execution_state(ctx, 0), 0x80000002u);

    T_CASE("what the page leaves open is fatal");
    T_TRAPS(rt_thread_execution_state(ctx, 0x00000004u), "ES_USER_PRESENT alone");
    T_TRAPS(rt_thread_execution_state(ctx, 0x00000040u), "ES_AWAYMODE_REQUIRED without ES_CONTINUOUS");
    T_TRAPS(rt_thread_execution_state(ctx, 0x00000041u), "ES_AWAYMODE_REQUIRED without ES_CONTINUOUS");
    T_TRAPS(rt_thread_execution_state(ctx, 0x80000008u), "outside the five ES_ values");
    T_TRAPS(rt_thread_execution_state(ctx, 0x00000100u), "outside the five ES_ values");
    T_TRAPS(rt_thread_execution_state(NULL, 0), "not a guest thread's");
    T_EQ(rt_thread_execution_state(ctx, 0), 0x80000002u);
}
