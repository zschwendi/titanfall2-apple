#include "rt/test.h"
#include "rt/env.h"
#include "rt/win/error.h"

/* The buffers are filled with 'Z' before a call, so a write or a missing write shows.  The variables are
 * set through the core (rt_env_set_a); the names are unique to this test. */

/* 1 if buf holds exactly the string `want` followed by its terminator. */
static int holds(const CHAR *buf, const CHAR *want)
{
    DWORD i;
    for (i = 0; want[i]; i++)
        if (buf[i] != want[i]) return 0;
    return buf[i] == 0;
}

/* 1 if the first n bytes of buf are still the preset 'Z'. */
static int untouched(const CHAR *buf, DWORD n)
{
    DWORD i;
    for (i = 0; i < n; i++)
        if (buf[i] != 'Z') return 0;
    return 1;
}

static void fill(CHAR *buf, DWORD n)
{
    DWORD i;
    for (i = 0; i < n; i++) buf[i] = 'Z';
}

TEST(kernel32, GetEnvironmentVariableA)
{
    CHAR buf[16];
    DWORD err = 0;

    T_CASE("a value is copied and its length returned, last error left alone");
    T_TRUE(rt_env_set_a("RT_TEST_GET_VAL", "hello", &err));
    T_EQ(err, 0);
    fill(buf, 16);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetEnvironmentVariableA, "RT_TEST_GET_VAL", buf, 16), 5);
    T_TRUE(holds(buf, "hello"));
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the name in another case finds it");
    fill(buf, 16);
    T_EQ(CALL(kernel32, GetEnvironmentVariableA, "rt_test_get_val", buf, 16), 5);
    T_TRUE(holds(buf, "hello"));

    T_CASE("the size query (NULL, 0) and a buffer one byte too small return the length plus one and leave a preset buffer alone, last error left alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetEnvironmentVariableA, "RT_TEST_GET_VAL", NULL, 0), 6);
    T_EQ(T_LAST_ERROR(), 0x1234);
    fill(buf, 16);
    T_EQ(CALL(kernel32, GetEnvironmentVariableA, "RT_TEST_GET_VAL", buf, 5), 6);
    T_TRUE(untouched(buf, 16));
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a variable that does not exist (0, ERROR_ENVVAR_NOT_FOUND, buffer untouched)");
    fill(buf, 16);
    T_EQ(CALL(kernel32, GetEnvironmentVariableA, "RT_TEST_GET_NOPE", buf, 16), 0);
    T_EQ(T_LAST_ERROR(), ERROR_ENVVAR_NOT_FOUND);
    T_TRUE(untouched(buf, 16));

    T_CASE("a variable set to the empty string (0 and last error left alone, after a preset of 0x1234)");
    T_TRUE(rt_env_set_a("RT_TEST_GET_EMPTY", "", &err));
    T_EQ(err, 0);
    fill(buf, 16);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetEnvironmentVariableA, "RT_TEST_GET_EMPTY", buf, 16), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(holds(buf, ""));
    /* Its size query says one byte, the terminator, and leaves last error alone as well
     * (docs/reviews/win-behaviour-check-4.md, Y2: Wine's kernel32/tests/environ.c presets
     * 0xdeadbeef and finds it again after both calls). */
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(kernel32, GetEnvironmentVariableA, "RT_TEST_GET_EMPTY", NULL, 0), 1);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);

    /* The cases the core stops on: fatal reports, each naming the case. */
    T_TRAPS(CALL(kernel32, GetEnvironmentVariableA, NULL, buf, 16), "the variable name is NULL");
    T_TRAPS(CALL(kernel32, GetEnvironmentVariableA, "A=B", buf, 16), "contains '=' after its first character");
    T_TRAPS(CALL(kernel32, GetEnvironmentVariableA, "a\xe9", buf, 16), "which is not ASCII");
    T_TRAPS(CALL(kernel32, GetEnvironmentVariableA, "RT_TEST_GET_VAL", NULL, 16), "NULL buffer of 16 bytes for a value of 5");
}
