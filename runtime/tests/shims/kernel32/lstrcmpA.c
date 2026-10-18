#include "rt/test.h"
#include "rt/locale.h"
#include "rt/win/error.h"

/* The expected values are the page's: lower-case letters and prefixes, whose order is the same by
 * code unit and in Windows's linguistic order.  Strings are compared exactly here, not by order
 * (see the shim). */
TEST(kernel32, lstrcmpA)
{
    WCHAR w[1024];
    char big[1101];
    unsigned logs;
    int i;

    T_CASE("returns -1, 0 and 1 in the order of the strings");
    T_EQ(CALL(kernel32, lstrcmpA, "abc", "abc"), 0);
    T_EQ(CALL(kernel32, lstrcmpA, "a", "b"), -1);
    T_EQ(CALL(kernel32, lstrcmpA, "b", "a"), 1);

    T_CASE("a prefix sorts first, and the empty string sorts before a non-empty one");
    T_EQ(CALL(kernel32, lstrcmpA, "a", "ab"), -1);
    T_EQ(CALL(kernel32, lstrcmpA, "ab", "a"), 1);
    T_EQ(CALL(kernel32, lstrcmpA, "", "a"), -1);
    T_EQ(CALL(kernel32, lstrcmpA, "a", ""), 1);
    T_EQ(CALL(kernel32, lstrcmpA, "", ""), 0);

    T_CASE("both strings NULL returns 0");
    T_EQ(CALL(kernel32, lstrcmpA, NULL, NULL), 0);

    T_CASE("only the first NULL returns -1");
    T_EQ(CALL(kernel32, lstrcmpA, NULL, "a"), -1);

    T_CASE("only the second NULL returns 1");
    T_EQ(CALL(kernel32, lstrcmpA, "a", NULL), 1);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, lstrcmpA, "abc", "abd"), -1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, lstrcmpA, "abc", "abc"), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, lstrcmpA, NULL, "a"), -1);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("unequal strings outside 0-9a-z: code-unit order, one warning");
    CALL(kernel32, SetLastError, 0x1234);
    logs = T_LOG_COUNT(RT_CH_IMPORT);               /* before the first call */
    T_EQ(CALL(kernel32, lstrcmpA, "a1", "b2"), -1); /* digits and lower case: no warning */
    T_EQ(CALL(kernel32, lstrcmpA, "B", "B"), 0);    /* equal: no warning */
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT), logs);
    T_EQ(CALL(kernel32, lstrcmpA, "a", "B"), 1);    /* the code-unit result, as before */
    T_EQ(CALL(kernel32, lstrcmpA, "a", "B"), 1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT) - logs, 1);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "lstrcmpA: unequal strings are ordered by code unit, not by Windows's word sort"));

    /* The stop depends on the code the core sets for a string that does not fit.  The NUL takes one
     * of the 1024 WCHARs: 1023 characters fit (the shim compares them), 1024 do not (the core
     * sets ERROR_INSUFFICIENT_BUFFER, checked here where the call returns). */
    for (i = 0; i < 1100; i++) big[i] = 'a';
    big[1100] = 0;
    big[1023] = 0;
    T_EQ(CALL(kernel32, lstrcmpA, big, big), 0);
    big[1023] = 'a';
    big[1024] = 0;
    T_EQ(rt_mb_to_wide(CP_ACP, 0, big, -1, w, 1024), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    big[1024] = 'a';
    T_TRAPS(CALL(kernel32, lstrcmpA, big, "a"), "a string of 1024 characters or more");
    T_TRAPS(CALL(kernel32, lstrcmpA, "a", big), "a string of 1024 characters or more");
}
