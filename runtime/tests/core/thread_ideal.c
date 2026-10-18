/* rt_thread_set_ideal_processor (core/thread_ctl.c; the specification is rt/thread.h).
 * Expected values: Microsoft's page of SetThreadIdealProcessor and Wine's
 * kernel32/tests/thread.c (test_thread_processor), as docs/reviews/census-rows.md 1.5 read
 * them; the starting value 0 is this runtime's choice (the header says why). */
#include "rt/test.h"
#include "rt/thread.h"
#include "rt/handle.h"
#include "rt/machine.h"

TEST(core, thread_ideal)
{
    Ctx *ctx = T_CTX();
    const DWORD count = rt_machine()->cpu_count;
    rt_set_last_error(ctx, 0x5EA1);

    T_CASE("a thread starts with processor 0; a set returns the previous value");
    T_EQ(rt_thread_set_ideal_processor(RT_PSEUDO_THREAD, 0), 0);
    if (count > 1) {
        T_EQ(rt_thread_set_ideal_processor(RT_PSEUDO_THREAD, count - 1), 0);
        T_EQ(rt_thread_set_ideal_processor(RT_PSEUDO_THREAD, 0), count - 1);
    }

    T_CASE("MAXIMUM_PROCESSORS (64) returns the stored value and changes nothing");
    T_EQ(rt_thread_set_ideal_processor(RT_PSEUDO_THREAD, 64), 0);
    T_EQ(rt_thread_set_ideal_processor(RT_PSEUDO_THREAD, 64), 0);
    T_EQ(T_LAST_ERROR(), 0x5EA1);                             /* success leaves last error alone */

    T_CASE("a processor above 64 fails with ERROR_INVALID_PARAMETER and changes nothing");
    T_EQ(rt_thread_set_ideal_processor(RT_PSEUDO_THREAD, 65), (DWORD)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(rt_thread_set_ideal_processor(RT_PSEUDO_THREAD, 64), 0);

    T_CASE("NULL and the process pseudo-handle fail with ERROR_INVALID_HANDLE");
    rt_set_last_error(ctx, 0x5EA1);
    T_EQ(rt_thread_set_ideal_processor(NULL, 0), (DWORD)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    rt_set_last_error(ctx, 0x5EA1);
    T_EQ(rt_thread_set_ideal_processor(RT_PSEUDO_PROCESS, 0), (DWORD)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* the stop: a processor the machine does not report, below 64 */
    if (count < 64)
        T_TRAPS(rt_thread_set_ideal_processor(RT_PSEUDO_THREAD, count), "rt_thread_set_ideal_processor: not implemented: processor");
}
