#include <stdlib.h>
#include <string.h>

#include "rt/test.h"
#include "rt/time.h"

/* The oracle is the core's zone bias, read here as the shim reads it: UTC = local + bias, so
 * local = UTC - bias, with bias in minutes times 600,000,000 units.  The zone is fixed to EST5
 * (five hours west of Greenwich, bias 300 minutes), so the bias is nonzero on every host and
 * the lower-bound check below runs.  The upper-bound check for a zone east of Greenwich (a
 * negative bias) does not run in this test. */
TEST(kernel32, FileTimeToLocalFileTime)
{
    FILETIME ft, lt, before;
    uint64_t v, e, local;
    int64_t bias;

    setenv("TZ", "EST5", 1);                            /* the core calls tzset() on each read */
    bias = (int64_t)rt_time_zone_bias_minutes() * 600000000;

    T_CASE("converts a UTC file time by the host's zone bias");
    T_TRUE(bias != 0);
    v = rt_time_filetime_from_unix(946684800, 0);       /* 2000-01-01 00:00:00 UTC */
    ft.dwLowDateTime = (DWORD)v;
    ft.dwHighDateTime = (DWORD)(v >> 32);
    memset(&lt, 0xAB, sizeof lt);
    T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &lt), TRUE);
    local = ((uint64_t)lt.dwHighDateTime << 32) | lt.dwLowDateTime;
    T_TRUE((int64_t)local + bias == (int64_t)v);        /* UTC = local + bias */
    T_EQ(local, (uint64_t)((int64_t)v - bias));
    T_EQ(((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime, v);   /* input unchanged */

    T_CASE("stores both halves of the local time");
    v = (UINT64_C(0x12345678) << 32) | UINT64_C(0x9ABCDEF0);
    ft.dwLowDateTime = (DWORD)v;
    ft.dwHighDateTime = (DWORD)(v >> 32);
    e = (uint64_t)((int64_t)v - bias);
    memset(&lt, 0xAB, sizeof lt);
    T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &lt), TRUE);
    T_EQ(lt.dwHighDateTime, (DWORD)(e >> 32));
    T_EQ(lt.dwLowDateTime, (DWORD)e);

    T_CASE("the two pointers may be the same");
    v = rt_time_filetime_from_unix(946684800, 0);
    ft.dwLowDateTime = (DWORD)v;
    ft.dwHighDateTime = (DWORD)(v >> 32);
    T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &ft), TRUE);
    T_EQ(((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime, (uint64_t)((int64_t)v - bias));

    T_CASE("a value of 0x8000000000000000 or more fails");
    ft.dwLowDateTime = 0;
    ft.dwHighDateTime = 0x80000000u;
    memset(&lt, 0xAB, sizeof lt);
    before = lt;
    T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &lt), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(memcmp(&lt, &before, sizeof lt), 0);
    ft.dwLowDateTime = 0xFFFFFFFFu;
    ft.dwHighDateTime = 0xFFFFFFFFu;
    T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &lt), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(memcmp(&lt, &before, sizeof lt), 0);

    T_CASE("a local time below zero or 0x8000000000000000 or more fails");
    if (bias > 0) {
        v = (uint64_t)bias - 1;                         /* local time -1 unit: below zero */
        ft.dwLowDateTime = (DWORD)v;
        ft.dwHighDateTime = (DWORD)(v >> 32);
        memset(&lt, 0xAB, sizeof lt);
        before = lt;
        T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &lt), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_EQ(memcmp(&lt, &before, sizeof lt), 0);
        v = (uint64_t)bias;                             /* local time exactly 0 */
        ft.dwLowDateTime = (DWORD)v;
        ft.dwHighDateTime = (DWORD)(v >> 32);
        memset(&lt, 0xAB, sizeof lt);
        T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &lt), TRUE);
        T_EQ(lt.dwLowDateTime, 0);
        T_EQ(lt.dwHighDateTime, 0);
    } else if (bias < 0) {
        v = (uint64_t)(INT64_MAX + bias);               /* local time INT64_MAX: the top */
        ft.dwLowDateTime = (DWORD)v;
        ft.dwHighDateTime = (DWORD)(v >> 32);
        memset(&lt, 0xAB, sizeof lt);
        T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &lt), TRUE);
        local = ((uint64_t)lt.dwHighDateTime << 32) | lt.dwLowDateTime;
        T_EQ(local, (uint64_t)INT64_MAX);
        v += 1;                                         /* local time 0x8000000000000000 */
        ft.dwLowDateTime = (DWORD)v;
        ft.dwHighDateTime = (DWORD)(v >> 32);
        memset(&lt, 0xAB, sizeof lt);
        before = lt;
        T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &lt), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_EQ(memcmp(&lt, &before, sizeof lt), 0);
    } else {
        v = (uint64_t)INT64_MAX;                        /* bias 0: the top of the input range */
        ft.dwLowDateTime = (DWORD)v;
        ft.dwHighDateTime = (DWORD)(v >> 32);
        T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &lt), TRUE);
        T_EQ(((uint64_t)lt.dwHighDateTime << 32) | lt.dwLowDateTime, v);
    }

    T_CASE("does not change last error on success");
    v = rt_time_filetime_from_unix(946684800, 0);
    ft.dwLowDateTime = (DWORD)v;
    ft.dwHighDateTime = (DWORD)(v >> 32);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FileTimeToLocalFileTime, &ft, &lt), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, FileTimeToLocalFileTime, NULL, &lt), "NULL lpFileTime");
    T_TRAPS(CALL(kernel32, FileTimeToLocalFileTime, &ft, NULL), "NULL lpLocalFileTime");
}
