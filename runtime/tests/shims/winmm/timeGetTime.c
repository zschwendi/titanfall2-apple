#include "rt/test.h"
#include "rt/time.h"

TEST(winmm, timeGetTime)
{
    T_CASE("two calls do not go backwards");
    DWORD prev = CALL(winmm, timeGetTime);
    for (unsigned i = 0; i < 100; i++) {
        DWORD next = CALL(winmm, timeGetTime);
        T_TRUE((DWORD)(next - prev) < 0x80000000u);      /* not behind the previous reading */
        prev = next;
    }

    T_CASE("the value lies within the (DWORD)rt_time_tick_ms() readings just before and after");
    uint64_t before = rt_time_tick_ms();
    DWORD got = CALL(winmm, timeGetTime);
    uint64_t after = rt_time_tick_ms();
    /* In DWORD arithmetic, got lies in [before, after] when its distance from before is no more
     * than the distance of after from before, across the 32-bit wrap. */
    T_TRUE((DWORD)((DWORD)got - (DWORD)before) <= (DWORD)((DWORD)after - (DWORD)before));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(winmm, timeGetTime);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
