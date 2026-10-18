/* Clocks (docs/RUNTIME.md section 8.1).
 *
 * Built in runtime/core/time.c (unit rt-core-time).  The comments are the specification.
 *
 * Two core clocks, and every time shim is arithmetic on one of them, so the clocks cannot
 * disagree and a recorded or fixed clock can be substituted behind this interface for tests
 * and trace comparison (rt_time_set_source):
 *
 *   QueryPerformanceCounter    rt_time_mono_100ns()            (frequency 10,000,000)
 *   GetTickCount[64], timeGetTime   rt_time_tick_ms()
 *   GetSystemTimeAsFileTime    rt_time_utc_100ns()
 *   rdtsc                      rt_time_mono_100ns() scaled to RtMachine.cpu_hz (3.0 GHz)
 *
 * None of these functions sets last error, takes a Ctx or needs a guest thread; all may be
 * called from any thread with any lock held (rt_time_zone_bias_minutes takes a lock of its
 * own for the C library's zone data).  A host clock that fails or reports nonsense is
 * fatal.
 */
#ifndef RT_TIME_H
#define RT_TIME_H

#include "runtime.h"
#include "rt/wintypes.h"

/* Called once by rt_init: record the monotonic clock's value at start-up, which is the
 * zero of rt_time_mono_100ns.  Fatal if called twice. */
void rt_time_init(void);

/* Monotonic time since rt_time_init in units of 100 ns.  Continuous: it keeps counting
 * while the host sleeps.  Never goes backwards, on any thread: each call returns the
 * larger of its reading and the largest value any call has returned.  Fatal before
 * rt_time_init. */
uint64_t rt_time_mono_100ns(void);

/* Wall-clock time in FILETIME units: 100 ns intervals since 1601-01-01 00:00 UTC.  Not
 * clamped: like GetSystemTimeAsFileTime it steps backwards when the host clock is set
 * back.  Works before rt_time_init. */
uint64_t rt_time_utc_100ns(void);

/* Replace the source of either clock, for a test or a trace comparison that needs known
 * times; NULL restores the host's.
 *   mono_100ns  called by every rt_time_mono_100ns, and so by rt_time_tick_ms, rt_rdtsc
 *               and the deadlines of the wait core.  It returns 100 ns units since "start".
 *               The never-backwards rule still holds while it is installed: a value below
 *               one already returned is replaced by that larger value.
 *   utc_100ns   called by every rt_time_utc_100ns; it returns FILETIME units, which are
 *               passed on as they are.
 * Every call, whatever its arguments, restarts the never-backwards maximum at 0: the first
 * reading after it is the new source's own value, even if smaller than earlier readings.
 * A switch is therefore a discontinuity, and values read before it must not be compared
 * with values read after it.
 * The sources are called on whatever thread reads the clock, possibly several at once, and
 * must not call back into this header.  The two pointers are stored atomically, but the
 * switch is not synchronised with readers: call it while no other thread reads a clock (a
 * test: before it starts threads) and while no thread is inside a timed wait.
 * NOT covered: rt_time_zone_bias_minutes always asks the host.  A thread sleeping in a
 * timed wait re-reads the clock only when its host sleep ends, so with a source that
 * stands still a timed wait never times out, and after a jump forward it times out only
 * once the host time it was sleeping for has passed (core/wait.c, TIMEOUTS).
 * May be called before rt_init.  Sets no last error; nothing is fatal. */
void rt_time_set_source(uint64_t (*mono_100ns)(void), uint64_t (*utc_100ns)(void));

/* Unix time to FILETIME units, for file times (stat) and anything else the host reports
 * as seconds and nanoseconds since 1970-01-01 00:00 UTC.  `nsec` is truncated to 100 ns,
 * the finest unit a FILETIME has.  Pure arithmetic: no clock is read and no source applies.
 * FATAL, never wrapped: nsec outside 0 to 999,999,999; a time before 1601-01-01 (sec below
 * -11,644,473,600); a time beyond what 64 bits of 100 ns units hold (after the year
 * 60,000).  A caller that passes times it does not control (a file's timestamps) checks the
 * range first if it wants an error instead. */
uint64_t rt_time_filetime_from_unix(int64_t sec, int64_t nsec);

/* Milliseconds since runtime start plus ten minutes (so that code which treats a small
 * tick count as "just booted" behaves as on a real machine): rt_time_mono_100ns() / 10000
 * + 600000. */
uint64_t rt_time_tick_ms(void);

/* The host's current offset from UTC in minutes, as the Bias of TIME_ZONE_INFORMATION
 * (UTC = local time + bias; positive west of Greenwich).  No daylight-saving rule: during
 * daylight time this is the daylight offset, so a GetTimeZoneInformation shim reports
 * StandardBias and DaylightBias as 0.  Always the host's zone and the host's wall clock,
 * also when rt_time_set_source installed another.  Fatal if the host's offset is not a
 * whole number of minutes. */
LONG rt_time_zone_bias_minutes(void);

/* The lifted `rdtsc` instruction (declared in harness/runtime.h as rt_rdtsc): the
 * monotonic clock scaled to the nominal RtMachine.cpu_hz, low half in eax and high half in
 * edx, both zero-extended. */
/* void rt_rdtsc(Ctx *ctx); */

#endif /* RT_TIME_H */
