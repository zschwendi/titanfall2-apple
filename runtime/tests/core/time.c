/* The two clocks, and the tick count and rdtsc built on them (docs/RUNTIME.md section 8.1).
 * The rest of the clock tests are in time_epoch.c (the FILETIME conversion), time_zone.c
 * (the time zone) and time_threads.c (the clocks read from several threads at once).
 *
 * The oracle is the host itself: mach_continuous_time, clock_gettime and mach_timebase_info,
 * read around each call.  The only constants taken from the specification are the rates: 10
 * MHz for the monotonic count (100 ns units), 3 GHz for rdtsc, 1 ms for the tick count.
 */
#include <errno.h>
#include <mach/mach_time.h>
#include <time.h>

#include "rt/test.h"
#include "rt/machine.h"
#include "rt/time.h"

#define UNITS_PER_SEC  UINT64_C(10000000)

static void sleep_ms(unsigned ms) {
    struct timespec want = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L }, left;
    while (nanosleep(&want, &left) != 0 && errno == EINTR) want = left;
}

/* The host's wall clock in FILETIME units, read directly. */
static uint64_t host_filetime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec + UINT64_C(11644473600)) * UNITS_PER_SEC + (uint64_t)ts.tv_nsec / 100;
}

/* 100 ns units in `ticks` of the host's continuous clock, by the host's own timebase. */
static uint64_t host_units(uint64_t ticks) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    return (uint64_t)((unsigned __int128)ticks * tb.numer / ((unsigned __int128)tb.denom * 100u));
}

/* A POSIX clock in 100 ns units, for differences only. */
static uint64_t posix_units(clockid_t id) {
    struct timespec ts;
    clock_gettime(id, &ts);
    return (uint64_t)ts.tv_sec * UNITS_PER_SEC + (uint64_t)ts.tv_nsec / 100;
}

/* rdtsc as the guest reads it: rdx:rax. */
static uint64_t tsc_of(const Ctx *c) {
    return ((uint64_t)c->r[2] << 32) | (c->r[0] & UINT64_C(0xFFFFFFFF));
}

/* Every part of a Ctx that rdtsc must leave alone: all registers but rax and rdx, the flags,
 * mxcsr, the xmm registers, the guest thread block pointer and the host pointer. */
static unsigned clobbered(const Ctx *a, const Ctx *b) {
    unsigned n = 0;
    for (unsigned i = 0; i < 16; i++)
        if (i != 0 && i != 2 && a->r[i] != b->r[i]) n++;
    n += (a->cf != b->cf) + (a->pf != b->pf) + (a->af != b->af) + (a->zf != b->zf);
    n += (a->sf != b->sf) + (a->of != b->of) + (a->df != b->df) + (a->pad != b->pad);
    n += (a->mxcsr != b->mxcsr);
    n += (memcmp(a->xmm, b->xmm, sizeof a->xmm) != 0);
    n += (a->gs_base != b->gs_base) + (a->host != b->host);
    return n;
}

static void garble(Ctx *c) {
    for (unsigned i = 0; i < 16; i++) c->r[i] = UINT64_C(0xDEAD0000BEEF0000) ^ ((uint64_t)i * 0x0101010101010101u);
    c->cf = 1; c->pf = 0; c->af = 1; c->zf = 0; c->sf = 1; c->of = 0; c->df = 0; c->pad = 0x5A;
    c->mxcsr = 0x1F80;
    for (unsigned i = 0; i < 16; i++)
        for (unsigned j = 0; j < 16; j++) c->xmm[i].b[j] = (uint8_t)(i * 17 + j);
    c->gs_base = UINT64_C(0x7000000000);
    c->host = (void *)(uintptr_t)0x55AA;
}

/* ---- the monotonic clock ---------------------------------------------------------------------- */

TEST(core, time_init_once)
{
    /* rt_init has started the clocks; a second start would move the zero. */
    T_TRAPS(rt_time_init(), "rt_time_init called twice");
}

TEST(core, time_mono_starts_near_zero)
{
    /* The zero is set by rt_init a moment before the test starts: far below a minute, and the
     * tick count is ten minutes plus that. */
    T_TRUE(rt_time_mono_100ns() < UINT64_C(600000000));
    uint64_t t = rt_time_tick_ms();
    T_TRUE(t >= 600000 && t < 660000);
}

TEST(core, time_mono_monotonic)
{
    uint64_t first = rt_time_mono_100ns(), prev = first;
    unsigned back = 0, steps = 0;
    for (unsigned i = 0; i < 2000000; i++) {
        uint64_t v = rt_time_mono_100ns();
        if (v < prev) back++;
        if (v != prev) steps++;
        prev = v;
    }
    T_EQ(back, 0);
    T_TRUE(prev > first);
    T_TRUE(steps > 1000);           /* it moves in many small steps, not a few large ones */
}

TEST(core, time_mono_scale_and_rate)
{
    /* An interval of about 50 ms, measured by rt_time_mono_100ns and, around the same reads, by
     * the host's continuous clock scaled with the host's timebase and by CLOCK_MONOTONIC.  The
     * two sets of reads are a few microseconds apart, so the best of five attempts must agree
     * with each oracle to 2 ms. */
    uint64_t best_host = UINT64_MAX, best_posix = UINT64_MAX;
    for (unsigned i = 0; i < 5; i++) {
        uint64_t c0 = mach_continuous_time();
        uint64_t m0 = rt_time_mono_100ns();
        uint64_t p0 = posix_units(CLOCK_MONOTONIC);
        sleep_ms(50);
        uint64_t m1 = rt_time_mono_100ns();
        uint64_t p1 = posix_units(CLOCK_MONOTONIC);
        uint64_t c1 = mach_continuous_time();

        uint64_t d = m1 - m0;
        uint64_t want_host = host_units(c1 - c0), want_posix = p1 - p0;
        uint64_t e1 = d > want_host ? d - want_host : want_host - d;
        uint64_t e2 = d > want_posix ? d - want_posix : want_posix - d;
        if (e1 < best_host) best_host = e1;
        if (e2 < best_posix) best_posix = e2;
    }
    T_TRUE(best_host <= 20000);
    T_TRUE(best_posix <= 20000);
}

TEST(core, time_tick_is_mono_over_10000_plus_ten_minutes)
{
    /* The tick count is read between two monotonic reads, so it must fall between the two
     * values it would have if computed from either of them. */
    unsigned bad = 0;
    for (unsigned i = 0; i < 1000; i++) {
        uint64_t m0 = rt_time_mono_100ns();
        uint64_t t = rt_time_tick_ms();
        uint64_t m1 = rt_time_mono_100ns();
        if (t < m0 / 10000 + 600000 || t > m1 / 10000 + 600000) bad++;
    }
    T_EQ(bad, 0);

    /* Milliseconds, not 100 ns units: a 100 ms sleep is about 100 ticks. */
    uint64_t t0 = rt_time_tick_ms();
    sleep_ms(100);
    uint64_t t1 = rt_time_tick_ms();
    T_TRUE(t1 - t0 >= 99 && t1 - t0 < 1000);
}

/* ---- rdtsc ------------------------------------------------------------------------------------ */

TEST(core, time_rdtsc_touches_only_rax_and_rdx)
{
    Ctx c;
    garble(&c);
    Ctx before = c;
    rt_rdtsc(&c);
    T_EQ(clobbered(&before, &c), 0);
    T_EQ(c.r[0] >> 32, 0);          /* eax written: rax's upper half is zero */
    T_EQ(c.r[2] >> 32, 0);          /* edx written: rdx's upper half is zero */
}

TEST(core, time_rdtsc_on_the_guest_context)
{
    Ctx *ctx = T_CTX();
    Ctx before = *ctx;
    rt_rdtsc(ctx);
    T_EQ(clobbered(&before, ctx), 0);
    T_EQ(ctx->r[0] >> 32, 0);
    T_EQ(ctx->r[2] >> 32, 0);
}

TEST(core, time_rdtsc_is_mono_at_3ghz)
{
    /* The count is the monotonic count times 3 * 10^9 / 10^7 = 300, exactly.  So it is a multiple
     * of 300 and lies between the monotonic reads taken either side of it. */
    unsigned bad = 0, notexact = 0;
    Ctx c;
    for (unsigned i = 0; i < 1000; i++) {
        uint64_t m0 = rt_time_mono_100ns();
        garble(&c);
        rt_rdtsc(&c);
        uint64_t m1 = rt_time_mono_100ns();
        uint64_t tsc = tsc_of(&c);
        if (tsc < m0 * 300 || tsc > m1 * 300) bad++;
        if (tsc % 300 != 0) notexact++;
    }
    T_EQ(bad, 0);
    T_EQ(notexact, 0);
}

TEST(core, time_rdtsc_nominal_rate)
{
    Ctx c;
    struct timespec h0, h1;
    memset(&c, 0, sizeof c);
    clock_gettime(CLOCK_MONOTONIC, &h0);
    rt_rdtsc(&c);
    uint64_t a = tsc_of(&c);
    sleep_ms(100);
    rt_rdtsc(&c);
    uint64_t d = tsc_of(&c) - a;
    clock_gettime(CLOCK_MONOTONIC, &h1);
    /* 3.0 GHz over 0.1 s is 3 * 10^8 counts, and a sleep is at least its time: the lower bound.
     * The upper bound is what the host's own clock measured around the two reads, at 3 counts
     * a nanosecond with 1% to spare: how long the sleep overshot on a busy machine does not
     * matter. */
    uint64_t host_ns = (uint64_t)(h1.tv_sec - h0.tv_sec) * 1000000000u + (uint64_t)h1.tv_nsec - (uint64_t)h0.tv_nsec;
    T_TRUE(d >= 299000000);
    T_TRUE(d <= host_ns * 3 + host_ns * 3 / 100);
}

/* ---- the clocks agree -------------------------------------------------------------------------- */

TEST(core, time_clocks_agree)
{
    uint64_t m0 = rt_time_mono_100ns(), u0 = rt_time_utc_100ns(), t0 = rt_time_tick_ms();
    Ctx c;
    memset(&c, 0, sizeof c);
    rt_rdtsc(&c);
    uint64_t c0 = tsc_of(&c);

    sleep_ms(100);

    uint64_t m1 = rt_time_mono_100ns(), u1 = rt_time_utc_100ns(), t1 = rt_time_tick_ms();
    rt_rdtsc(&c);
    uint64_t c1 = tsc_of(&c);

    uint64_t dm = m1 - m0, du = u1 - u0, dt = t1 - t0, dc = c1 - c0;
    uint64_t wall_vs_mono = du > dm ? du - dm : dm - du;
    T_TRUE(wall_vs_mono < 500000);                          /* 50 ms, the wall clock and the monotonic count */
    uint64_t tick_vs_mono = dt > dm / 10000 ? dt - dm / 10000 : dm / 10000 - dt;
    T_TRUE(tick_vs_mono <= 2);                              /* milliseconds of the same interval */
    uint64_t tsc_want = dm * 300;
    uint64_t tsc_vs_mono = dc > tsc_want ? dc - tsc_want : tsc_want - dc;
    T_TRUE(tsc_vs_mono < 6000000);                          /* 2 ms at 3 GHz */
}

/* ---- the wall clock --------------------------------------------------------------------------- */

TEST(core, time_utc_tracks_the_host_clock)
{
    /* Not checked for monotonicity: like GetSystemTimeAsFileTime, the wall clock follows the host
     * clock, which the host may set back.  Checked against the host's own clock, read around
     * each call, to within a second. */
    unsigned bad = 0;
    uint64_t u = 0;
    for (unsigned i = 0; i < 1000; i++) {
        uint64_t before = host_filetime();
        u = rt_time_utc_100ns();
        uint64_t after = host_filetime();
        if (u + UNITS_PER_SEC < before || u > after + UNITS_PER_SEC) bad++;
    }
    T_EQ(bad, 0);

    /* Today, by the host's calendar: between 2020 and 2100, and within two seconds of time(). */
    time_t now = time(NULL);
    T_TRUE(now > 1577836800 && now < 4102444800);
    int64_t from_u = (int64_t)(u / UNITS_PER_SEC) - 11644473600;
    int64_t gap = from_u > (int64_t)now ? from_u - (int64_t)now : (int64_t)now - from_u;
    T_TRUE(gap <= 2);
}

/* ---- a substitute source (rt_time_set_source) ------------------------------------------------ */

static uint64_t fake_mono_v, fake_utc_v;
static unsigned fake_mono_calls, fake_utc_calls;
static uint64_t fake_mono(void) { fake_mono_calls++; return fake_mono_v; }
static uint64_t fake_utc(void)  { fake_utc_calls++; return fake_utc_v; }

TEST(core, time_source)
{
    const uint64_t hz = rt_machine()->cpu_hz;
    sleep_ms(2);
    const uint64_t host0 = rt_time_mono_100ns();
    T_REQUIRE(host0 > 12345);                                  /* the fake values below are smaller */

    /* Both clocks from the test.  The first reading is the source's own value although the
     * host clock had already returned a larger one: the maximum restarts at a switch. */
    fake_mono_v = 12345;
    fake_utc_v = UINT64_C(132223104000000000);
    rt_time_set_source(fake_mono, fake_utc);
    T_EQ(rt_time_mono_100ns(), 12345);
    T_EQ(fake_mono_calls, 1);                                  /* one reading of the source per call */
    T_EQ(rt_time_utc_100ns(), UINT64_C(132223104000000000));
    T_EQ(fake_utc_calls, 1);
    /* The tick count and rdtsc are arithmetic on the same source. */
    T_EQ(rt_time_tick_ms(), 600001);
    Ctx c;
    memset(&c, 0xEE, sizeof c);
    rt_rdtsc(&c);
    T_EQ(c.r[0], 12345 * hz / UNITS_PER_SEC);
    T_EQ(c.r[2], 0);
    fake_mono_v = UINT64_C(50000000);                          /* five seconds */
    T_EQ(rt_time_mono_100ns(), 50000000);
    T_EQ(rt_time_tick_ms(), 605000);
    fake_mono_v = UINT64_C(0x123456789AB);                     /* beyond 32 bits of rdtsc */
    memset(&c, 0, sizeof c);
    rt_rdtsc(&c);
    uint64_t tsc = (uint64_t)((unsigned __int128)UINT64_C(0x123456789AB) * hz / UNITS_PER_SEC);
    T_EQ(c.r[0], (uint32_t)tsc);
    T_EQ(c.r[2], tsc >> 32);

    /* The monotonic clock never goes backwards, also with a source that does ... */
    fake_mono_v = 49999999;
    T_EQ(rt_time_mono_100ns(), UINT64_C(0x123456789AB));
    T_EQ(rt_time_tick_ms(), UINT64_C(0x123456789AB) / 10000 + 600000);
    /* ... and the wall clock is passed on as it is, backwards included. */
    fake_utc_v = 5;
    T_EQ(rt_time_utc_100ns(), 5);

    /* One clock at a time: NULL is the host's.  Every call restarts the maximum. */
    uint64_t before = host_filetime();
    rt_time_set_source(fake_mono, NULL);
    T_EQ(rt_time_mono_100ns(), 49999999);
    uint64_t utc = rt_time_utc_100ns();
    T_TRUE(utc >= before && utc <= host_filetime());
    unsigned calls = fake_mono_calls;
    rt_time_set_source(NULL, fake_utc);
    uint64_t host1 = rt_time_mono_100ns();
    T_TRUE(host1 >= host0 && host1 < host0 + 60 * UNITS_PER_SEC);
    T_EQ(fake_mono_calls, calls);                              /* the source is no longer asked */
    T_EQ(rt_time_utc_100ns(), 5);

    /* Back to the host for both. */
    calls = fake_utc_calls;
    rt_time_set_source(NULL, NULL);
    T_TRUE(rt_time_mono_100ns() >= host1);
    utc = rt_time_utc_100ns();
    T_TRUE(utc >= before && utc <= host_filetime());
    T_EQ(fake_utc_calls, calls);
}

