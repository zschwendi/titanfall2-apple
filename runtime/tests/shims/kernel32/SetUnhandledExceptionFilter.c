#include "rt/test.h"
#include "rt/crash.h"

/* Any non-zero value serves as a filter: the shim stores it and never calls it (PLAN A).  The
 * oracle is rt_uef_get(), the core's stored filter, read before and after each call. */
#define FILTER_A ((LPTOP_LEVEL_EXCEPTION_FILTER)0x140001000)
#define FILTER_B ((LPTOP_LEVEL_EXCEPTION_FILTER)0x140002000)

TEST(kernel32, SetUnhandledExceptionFilter)
{
    GUESTFN orig, before, prev, first, second;

    orig = rt_uef_get();

    T_CASE("returns the filter stored before and stores the new one");
    before = rt_uef_get();
    prev = CALL(kernel32, SetUnhandledExceptionFilter, FILTER_A);
    T_EQ(prev, before);
    T_EQ(rt_uef_get(), FILTER_A);

    T_CASE("stores 0");
    prev = CALL(kernel32, SetUnhandledExceptionFilter, 0);
    T_EQ(prev, FILTER_A);
    T_EQ(rt_uef_get(), 0);

    T_CASE("two calls in a row give the first value back");
    before = rt_uef_get();
    first = CALL(kernel32, SetUnhandledExceptionFilter, FILTER_A);
    second = CALL(kernel32, SetUnhandledExceptionFilter, FILTER_B);
    T_EQ(first, before);
    T_EQ(second, FILTER_A);
    T_EQ(rt_uef_get(), FILTER_B);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, SetUnhandledExceptionFilter, FILTER_A);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetUnhandledExceptionFilter, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Put back the filter the process had before the test. */
    CALL(kernel32, SetUnhandledExceptionFilter, orig);
}
