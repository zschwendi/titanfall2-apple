/* Clocks (docs/RUNTIME.md section 8.1).  Interface and specification: rt/time.h.
 *
 * Three host sources, and everything else in the header is arithmetic on them:
 *
 *   rt_time_mono_100ns()          mach_continuous_time(), which keeps counting through sleep
 *                                 (mach_absolute_time does not, so it is not used), scaled
 *                                 from host ticks to 100 ns units with mach_timebase_info and
 *                                 counted from the value rt_time_init records.
 *   rt_time_utc_100ns()           clock_gettime(CLOCK_REALTIME), the host wall clock, in
 *                                 FILETIME units.
 *   rt_time_zone_bias_minutes()   localtime_r of the same wall clock: the host's UTC offset.
 *
 * rt_time_tick_ms() and rt_rdtsc() are computed from rt_time_mono_100ns(), so the clocks
 * cannot disagree with each other.
 *
 * Overflow.  Host ticks are scaled as ticks * numer / (denom * 100).  The product is formed
 * in unsigned __int128: a 64-bit tick count times a 32-bit numerator is below 2^96, so it
 * cannot overflow.  The quotient is then checked to fit in 64 bits, and the runtime stops if
 * it does not.  The ticks are counted from start-up, so the quotient stays small for decades
 * of uptime; the check is there for a host that reports nonsense.  rt_rdtsc multiplies the
 * 100 ns count by cpu_hz (3 * 10^9) in 128 bits for the same reason.
 *
 * Monotonicity.  rt_time_mono_100ns never goes backwards, on any thread.  Each call returns
 * the larger of the host reading it took and the largest value any call has returned so far
 * (a compare-and-swap maximum).  On a host whose continuous clock does not go backwards, and
 * Apple's is meant not to, the maximum changes nothing.  It keeps the guarantee when a call
 * is delayed between reading the host and returning, so another thread may already have
 * returned a later value.  rt_time_utc_100ns is the wall clock and is not clamped: like
 * GetSystemTimeAsFileTime, it steps backwards when the host clock is set back.
 *
 * A substitute source (rt_time_set_source) replaces the first two readings and nothing else:
 * the monotonic maximum is applied to its values too, and is restarted at every switch.
 *
 * rt_time_filetime_from_unix is the epoch conversion behind rt_time_utc_100ns, and the file
 * unit's conversion for file times.
 */
#include <errno.h>
#include <mach/kern_return.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "core.h"
#include "rt/machine.h"
#include "rt/time.h"

/* Seconds from 1601-01-01 00:00 UTC, the FILETIME epoch, to 1970-01-01 00:00 UTC: 369 years
 * with 89 leap days, 134,774 days.  (The value 116444736000000000 in 100 ns units is the
 * same number.) */
#define SECS_1601_TO_1970  INT64_C(11644473600)
#define UNITS_PER_SEC     UINT64_C(10000000)      /* FILETIME and monotonic units in a second */
#define UNITS_PER_MS      UINT64_C(10000)         /* 100 ns units in a millisecond */
#define TICK_BASE_MS      UINT64_C(600000)        /* ten minutes, added to the tick count */

/* Written once by rt_time_init, before any other thread can exist, and never again. */
static uint64_t tb_numer, tb_denom;     /* host ticks to nanoseconds: ns = ticks * numer / denom */
static uint64_t base_ticks;             /* mach_continuous_time() at start-up: the zero */
static int      started;

/* The largest value rt_time_mono_100ns has returned, on any thread. */
static uint64_t mono_max;

/* tzset() and localtime_r() must not run while another thread changes the zone. */
static pthread_mutex_t zone_lock = PTHREAD_MUTEX_INITIALIZER;

/* Substitute sources (rt_time_set_source); NULL means the host.  Loaded and stored atomically. */
static uint64_t (*src_mono)(void);
static uint64_t (*src_utc)(void);

void rt_time_set_source(uint64_t (*mono_100ns)(void), uint64_t (*utc_100ns)(void)) {
    __atomic_store_n(&src_mono, mono_100ns, __ATOMIC_SEQ_CST);
    __atomic_store_n(&src_utc, utc_100ns, __ATOMIC_SEQ_CST);
    /* A switch is a discontinuity: the new source's first value is returned as it is. */
    __atomic_store_n(&mono_max, 0, __ATOMIC_SEQ_CST);
}

void rt_time_init(void) {
    if (__atomic_load_n(&started, __ATOMIC_ACQUIRE)) rt_fatal("rt_time_init called twice");
    mach_timebase_info_data_t tb;
    kern_return_t kr = mach_timebase_info(&tb);
    if (kr != KERN_SUCCESS) rt_fatal("mach_timebase_info failed: kern_return_t %d", (int)kr);
    if (tb.numer == 0 || tb.denom == 0) rt_fatal("mach_timebase_info gave %u/%u", tb.numer, tb.denom);
    tb_numer = tb.numer;
    tb_denom = tb.denom;
    base_ticks = mach_continuous_time();
    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
}

/* The host's continuous clock since rt_time_init, in 100 ns units.  Not clamped. */
static uint64_t host_units_since_start(void) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) rt_fatal("a clock was read before rt_time_init");
    uint64_t now = mach_continuous_time();
    if (now < base_ticks)
        rt_fatal("the host's continuous clock went backwards: %llu ticks now, %llu at start-up",
                 (unsigned long long)now, (unsigned long long)base_ticks);
    unsigned __int128 n = (unsigned __int128)(now - base_ticks) * tb_numer
                          / ((unsigned __int128)tb_denom * 100u);
    if (n > UINT64_MAX) rt_fatal("the monotonic clock is beyond 64 bits of 100 ns units");
    return (uint64_t)n;
}

uint64_t rt_time_mono_100ns(void) {
    uint64_t (*src)(void) = __atomic_load_n(&src_mono, __ATOMIC_SEQ_CST);
    if (src && !__atomic_load_n(&started, __ATOMIC_ACQUIRE)) rt_fatal("a clock was read before rt_time_init");
    uint64_t v = src ? src() : host_units_since_start();
    uint64_t last = __atomic_load_n(&mono_max, __ATOMIC_SEQ_CST);
    while (v > last) {
        if (__atomic_compare_exchange_n(&mono_max, &last, v, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return v;
        /* The failed exchange loaded the current maximum into `last`; test again. */
    }
    return last;
}

uint64_t rt_time_utc_100ns(void) {
    uint64_t (*src)(void) = __atomic_load_n(&src_utc, __ATOMIC_SEQ_CST);
    if (src) return src();
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        rt_fatal("clock_gettime(CLOCK_REALTIME) failed: %s", strerror(errno));
    return rt_time_filetime_from_unix((int64_t)ts.tv_sec, (int64_t)ts.tv_nsec);
}

/* Unix time (seconds since 1970-01-01 UTC, nanoseconds 0 to 999,999,999) to FILETIME: 100 ns
 * intervals since 1601-01-01 UTC.  The nanoseconds are truncated to 100 ns units, the finest
 * unit a FILETIME has.  A time before 1601 or past what 64 bits of 100 ns units hold is refused,
 * not wrapped. */
uint64_t rt_time_filetime_from_unix(int64_t sec, int64_t nsec) {
    if (nsec < 0 || nsec >= 1000000000)
        rt_fatal("time conversion: %lld nanoseconds is out of range", (long long)nsec);
    if (sec < -SECS_1601_TO_1970)
        rt_fatal("time conversion: %lld seconds from 1970 is before 1601-01-01 (the FILETIME epoch)",
                 (long long)sec);
    int64_t since1601;
    if (__builtin_add_overflow(sec, SECS_1601_TO_1970, &since1601))
        rt_fatal("time conversion: %lld seconds from 1970 is beyond what FILETIME can hold", (long long)sec);
    uint64_t units = (uint64_t)nsec / 100;
    uint64_t s = (uint64_t)since1601;
    if (s > (UINT64_MAX - units) / UNITS_PER_SEC)
        rt_fatal("time conversion: %lld seconds from 1970 is beyond what FILETIME can hold", (long long)sec);
    return s * UNITS_PER_SEC + units;
}

uint64_t rt_time_tick_ms(void) {
    /* rt_time_mono_100ns() / 10000 is at most about 1.8 * 10^15, so adding ten minutes
     * cannot wrap. */
    return rt_time_mono_100ns() / UNITS_PER_MS + TICK_BASE_MS;
}

LONG rt_time_zone_bias_minutes(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        rt_fatal("clock_gettime(CLOCK_REALTIME) failed: %s", strerror(errno));
    time_t when = ts.tv_sec;
    struct tm local;
    pthread_mutex_lock(&zone_lock);
    tzset();                                    /* pick up a zone the user changed while running */
    struct tm *ok = localtime_r(&when, &local);
    int err = errno;
    pthread_mutex_unlock(&zone_lock);
    if (!ok) rt_fatal("localtime_r failed: %s", strerror(err));

    /* tm_gmtoff is seconds east of UTC, for the offset in effect at this instant.  The
     * result is Bias in the sense of TIME_ZONE_INFORMATION: UTC = local + Bias, in minutes,
     * positive west of Greenwich.  No daylight-saving rule is reported (RUNTIME.md 8.1), so
     * during daylight time this is the daylight offset.  As I read the Microsoft documentation,
     * Windows keeps the standard offset in Bias and the daylight difference in DaylightBias.
     * A GetTimeZoneInformation shim built on this value must therefore report StandardBias
     * and DaylightBias as zero, or a caller that adds them gets the offset twice. */
    long off = local.tm_gmtoff;
    if (off % 60 != 0)
        rt_fatal("the host's UTC offset is %ld seconds, not a whole number of minutes; "
                 "TIME_ZONE_INFORMATION holds whole minutes", off);
    int64_t bias = -(int64_t)(off / 60);
    if (bias < INT32_MIN || bias > INT32_MAX) rt_fatal("the host's UTC offset %ld does not fit a LONG", off);
    return (LONG)bias;
}

/* The lifted rdtsc: the monotonic clock scaled to the nominal rate of the machine (RtMachine
 * cpu_hz, 3 * 10^9 Hz, which the registry also reports).  Its counter is the 64-bit count
 * since runtime start.  Real TSC is also a 64-bit counter that wraps at 2^64, which at 3 GHz
 * is after about 195 years, so the wrap here is the hardware's behaviour too.  eax gets the
 * low half and edx the high half; a 32-bit write zero-extends on x86-64, so both upper halves
 * are zero.  Flags and every other register are untouched. */
void rt_rdtsc(Ctx *ctx) {
    const RtMachine *m = rt_machine();
    if (m->cpu_hz == 0) rt_fatal("rt_rdtsc: the machine's nominal clock is 0 Hz");
    unsigned __int128 t = (unsigned __int128)rt_time_mono_100ns() * m->cpu_hz / UNITS_PER_SEC;
    uint64_t tsc = (uint64_t)t;
    ctx->r[0] = (uint32_t)tsc;              /* eax, zero-extended into rax */
    ctx->r[2] = tsc >> 32;                  /* edx, zero-extended into rdx */
}
