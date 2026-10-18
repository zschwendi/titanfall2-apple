#include "rt/test.h"

TEST(winmm, timeBeginPeriod)
{
    /* The runtime may log on the thread channel by itself, so count from here. */
    unsigned base = T_LOG_COUNT(RT_CH_THREAD);

    T_CASE("a period of 1 ms or more succeeds without effect");
    T_EQ(CALL(winmm, timeBeginPeriod, 1), TIMERR_NOERROR);
    T_EQ(CALL(winmm, timeBeginPeriod, 15), TIMERR_NOERROR);
    T_EQ(CALL(winmm, timeBeginPeriod, 0xFFFFFFFFu), TIMERR_NOERROR);

    T_CASE("a period of 0 returns TIMERR_NOCANDO");
    unsigned before_zero = T_LOG_COUNT(RT_CH_THREAD);
    T_EQ(CALL(winmm, timeBeginPeriod, 0), TIMERR_NOCANDO);
    T_EQ(T_LOG_COUNT(RT_CH_THREAD), before_zero);        /* the rejected call logs nothing */

    T_CASE("the first accepted call is logged once at debug level");
    /* The calls of the first case logged exactly one line, for the first of them. */
    T_TRUE(T_LOGGED(RT_CH_THREAD, "timeBeginPeriod(1)"));
    T_EQ(T_LOG_COUNT(RT_CH_THREAD), base + 1);
    T_EQ(CALL(winmm, timeBeginPeriod, 1), TIMERR_NOERROR);   /* one more accepted call, same key */
    T_EQ(CALL(winmm, timeBeginPeriod, 7), TIMERR_NOERROR);
    T_EQ(T_LOG_COUNT(RT_CH_THREAD), base + 1);
    T_TRUE(!T_LOGGED(RT_CH_THREAD, "timeBeginPeriod(7)"));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(winmm, timeBeginPeriod, 1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(winmm, timeBeginPeriod, 0);                     /* the out-of-range failure does not set it either */
    T_EQ(T_LAST_ERROR(), 0x1234);
}
