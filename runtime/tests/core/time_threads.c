/* The clocks read from several host threads at once.  rt/time.h promises that the monotonic
 * clock is thread-safe and never goes backwards.
 *
 * Each worker keeps its own tally, and the main thread checks the totals.  The T_* checks
 * are not thread-safe, so no worker calls them.
 *
 * What is checked, per worker and across workers:
 *   back      a read is smaller than an earlier read by the same thread, or a later read
 *             in the same round is smaller than the earlier one;
 *   stale     a read that starts after another thread's read has returned (and been
 *             published) is smaller than that value, which would break the guarantee that
 *             the monotonic clock never goes backwards on any thread;
 *   bracket   the tick count or rdtsc, read between two monotonic reads, is outside what
 *             those reads imply;
 *   zone_bad  the zone bias differs from the value read before the threads started;
 *   utc_bad   the wall clock is outside 2020 to 2100.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#include "rt/test.h"
#include "rt/time.h"

#define THREADS 8
#define ROUNDS  50000

/* The largest values returned so far, by any thread: published after each call returns. */
static _Atomic uint64_t pub_mono, pub_tick, pub_tsc;

/* The zone bias, read once before the threads start. */
static LONG zone_expected;

struct Tally {
    uint64_t rounds, back, stale, bracket, zone_bad, utc_bad;
};

static void publish_max(_Atomic uint64_t *pub, uint64_t v) {
    uint64_t cur = atomic_load(pub);
    while (v > cur && !atomic_compare_exchange_weak(pub, &cur, v)) { }
}

static void *worker(void *arg) {
    struct Tally *t = arg;
    uint64_t prev_m = 0, prev_t = 0, prev_c = 0;
    for (uint64_t i = 0; i < ROUNDS; i++) {
        /* What other threads have already returned before this round starts. */
        uint64_t floor_m = atomic_load(&pub_mono);
        uint64_t floor_t = atomic_load(&pub_tick);
        uint64_t floor_c = atomic_load(&pub_tsc);

        Ctx c;
        memset(&c, 0, sizeof c);
        uint64_t m0 = rt_time_mono_100ns();
        uint64_t tk = rt_time_tick_ms();
        rt_rdtsc(&c);
        uint64_t tsc = ((uint64_t)c.r[2] << 32) | (c.r[0] & UINT64_C(0xFFFFFFFF));
        uint64_t m1 = rt_time_mono_100ns();

        if (m0 < prev_m || m1 < m0) t->back++;
        if (tk < prev_t) t->back++;
        if (tsc < prev_c) t->back++;
        if (m0 < floor_m || tk < floor_t || tsc < floor_c) t->stale++;
        if (tk < m0 / 10000 + 600000 || tk > m1 / 10000 + 600000) t->bracket++;
        if (tsc < m0 * 300 || tsc > m1 * 300) t->bracket++;

        prev_m = m1;
        prev_t = tk;
        prev_c = tsc;
        publish_max(&pub_mono, m1);
        publish_max(&pub_tick, tk);
        publish_max(&pub_tsc, tsc);

        if (i % 16 == 0) {
            if (rt_time_zone_bias_minutes() != zone_expected) t->zone_bad++;
            uint64_t u = rt_time_utc_100ns();
            /* 2020-01-01 and 2100-01-01 as FILETIME. */
            if (u < UINT64_C(132223104000000000) || u > UINT64_C(157469184000000000)) t->utc_bad++;
        }
        t->rounds++;
    }
    return NULL;
}

TEST(core, time_concurrent_readers)
{
    zone_expected = rt_time_zone_bias_minutes();

    pthread_t th[THREADS];
    struct Tally tally[THREADS];
    memset(tally, 0, sizeof tally);
    unsigned created = 0;
    for (unsigned i = 0; i < THREADS; i++) {
        if (pthread_create(&th[i], NULL, worker, &tally[i]) != 0) break;
        created++;
    }
    T_EQ(created, THREADS);
    for (unsigned i = 0; i < created; i++) pthread_join(th[i], NULL);

    struct Tally sum;
    memset(&sum, 0, sizeof sum);
    for (unsigned i = 0; i < created; i++) {
        sum.rounds += tally[i].rounds;
        sum.back += tally[i].back;
        sum.stale += tally[i].stale;
        sum.bracket += tally[i].bracket;
        sum.zone_bad += tally[i].zone_bad;
        sum.utc_bad += tally[i].utc_bad;
    }
    T_EQ(sum.rounds, (uint64_t)THREADS * ROUNDS);
    T_EQ(sum.back, 0);
    T_EQ(sum.stale, 0);
    T_EQ(sum.bracket, 0);
    T_EQ(sum.zone_bad, 0);
    T_EQ(sum.utc_bad, 0);
}
