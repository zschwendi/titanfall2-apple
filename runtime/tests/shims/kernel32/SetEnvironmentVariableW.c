#include "rt/test.h"
#include "rt/env.h"
#include "rt/win/error.h"

/* What the guest sees is read back through the core: rt_env_get_w for a value, and the environment
 * block (rt_env_block_w, the snapshot GetEnvironmentStringsW gives) for spelling and place. */

static WCHAR got[RT_ENV_VALUE_MAX + 1];
static WCHAR big[RT_ENV_VALUE_MAX + 2];

/* The core's count for `name` (its value is copied into got); *err is the core's error. */
static DWORD read_back(const WCHAR *name, DWORD *err)
{
    *err = 0;
    return rt_env_get_w(name, got, RT_ENV_VALUE_MAX + 1, err);
}

/* 1 if `name` exists with exactly the value `want`. */
static int holds(const WCHAR *name, const WCHAR *want)
{
    DWORD err = 0, n, i;
    n = read_back(name, &err);
    if (err) return 0;
    for (i = 0; want[i]; i++)
        if (i >= n || got[i] != want[i]) return 0;
    return i == n;
}

/* 1 if `name` exists; an empty value exists too. */
static int exists(const WCHAR *name)
{
    DWORD err = 0;
    read_back(name, &err);
    return err == 0;
}

static WCHAR fold(WCHAR c)
{
    return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c;
}

/* The index of the entry for `name` in the block (names compare ASCII case-insensitively), or -1.
 * *exact is 1 if the entry is spelled exactly as `name`. */
static int block_index(const WCHAR *block, const WCHAR *name, int *exact)
{
    const WCHAR *e = block, *a, *b;
    int idx;

    *exact = 0;
    for (idx = 0; *e; idx++) {
        a = e;
        b = name;
        while (*a && *a != '=' && *b && fold(*a) == fold(*b)) { a++; b++; }
        if (*b == 0 && *a == '=') {
            *exact = 1;
            for (a = e, b = name; *b; a++, b++)
                if (*a != *b) *exact = 0;
            return idx;
        }
        while (*e) e++;
        e++;
    }
    return -1;
}

static int block_count(const WCHAR *block)
{
    const WCHAR *e = block;
    int n = 0;
    while (*e) {
        while (*e) e++;
        e++;
        n++;
    }
    return n;
}

TEST(kernel32, SetEnvironmentVariableW)
{
    DWORD err = 0;
    WCHAR *block;
    int exact, i1, i2, j1;
    DWORD i;

    T_CASE("sets a new variable and reads it back with its value");
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_NEW", u"one"), TRUE);
    T_TRUE(holds(u"RT_TEST_NEW", u"one"));

    T_CASE("replaces the value of an existing variable, matching the name without regard to case");
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"rt_test_new", u"two"), TRUE);
    T_TRUE(holds(u"RT_TEST_NEW", u"two"));
    T_TRUE(!holds(u"RT_TEST_NEW", u"one"));

    T_CASE("keeps the spelling and the place of an existing variable and adds a new one at the end of the block");
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_P1", u"1"), TRUE);
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_P2", u"2"), TRUE);
    block = rt_env_block_w(&err);
    T_EQ(err, 0);
    i1 = block_index(block, u"RT_TEST_P1", &exact);
    T_TRUE(exact);
    i2 = block_index(block, u"RT_TEST_P2", &exact);
    T_TRUE(exact);
    T_TRUE(i1 >= 0 && i1 < i2);
    T_EQ(i2, block_count(block) - 1);
    rt_env_block_free(block);
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"rt_test_p1", u"3"), TRUE);
    T_TRUE(holds(u"RT_TEST_P1", u"3"));
    block = rt_env_block_w(&err);
    j1 = block_index(block, u"RT_TEST_P1", &exact);
    T_TRUE(exact);
    T_EQ(j1, i1);
    rt_env_block_free(block);

    T_CASE("a NULL value deletes the variable");
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_DEL", u"x"), TRUE);
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_DEL", NULL), TRUE);
    T_TRUE(!exists(u"RT_TEST_DEL"));

    T_CASE("deleting a variable that does not exist fails with ERROR_ENVVAR_NOT_FOUND");
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_NOPE", NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ENVVAR_NOT_FOUND);

    T_CASE("an empty value sets the variable to the empty string and it still exists");
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_EMPTY", u""), TRUE);
    T_EQ(read_back(u"RT_TEST_EMPTY", &err), 0);
    T_EQ(err, 0);
    T_TRUE(exists(u"RT_TEST_EMPTY"));

    T_CASE("a value of RT_ENV_VALUE_MAX characters is accepted and one more fails with ERROR_INVALID_PARAMETER");
    for (i = 0; i < RT_ENV_VALUE_MAX; i++) big[i] = u'x';
    big[RT_ENV_VALUE_MAX] = 0;
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_BIG", big), TRUE);
    T_EQ(read_back(u"RT_TEST_BIG", &err), RT_ENV_VALUE_MAX);
    big[RT_ENV_VALUE_MAX] = u'x';
    big[RT_ENV_VALUE_MAX + 1] = 0;
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_BIG", big), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a value of any text, non-ASCII included, is kept exactly");
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_UNI", u"caf\u00e9 \u4e2d \U0001F600"), TRUE);
    T_TRUE(holds(u"RT_TEST_UNI", u"caf\u00e9 \u4e2d \U0001F600"));

    T_CASE("an empty name with a value fails with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"", u"x"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("an empty name with NULL fails with ERROR_ENVVAR_NOT_FOUND");
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"", NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ENVVAR_NOT_FOUND);

    T_CASE("a name beginning with '=' is stored like any other");
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"=C:", u"C:\\dir"), TRUE);
    T_TRUE(holds(u"=C:", u"C:\\dir"));
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"=C:", NULL), TRUE);
    T_TRUE(!exists(u"=C:"));

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetEnvironmentVariableW, u"RT_TEST_KEEP", u"v"), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The cases the shim refuses: fatal reports, each naming the case. */
    T_TRAPS(CALL(kernel32, SetEnvironmentVariableW, NULL, u"x"), "NULL name");
    T_TRAPS(CALL(kernel32, SetEnvironmentVariableW, u"A=B", u"x"), "'=' after the first character of the name");
    T_TRAPS(CALL(kernel32, SetEnvironmentVariableW, u"a\u00e9", u"x"), "a name that is not ASCII");
}
