/* rt_env_expand_a (core/env.c; the specification is rt/env.h).
 *
 * Where the expected values come from: the results Wine's kernel32/tests/environ.c
 * (test_ExpandEnvironmentStringsA) requires of Windows.  Its table sets TVAR to "WINE", leaves
 * TVAR2 unset, fills the buffer with "abcdefghijklmnopqrstuv" before each call and requires a
 * return value and a buffer content; the rows below are that table's, as facts (source string,
 * size, result), for every source string it has and for the sizes around each limit.  The
 * single-pass case (a value that holds a reference) is the same test's "IndirectVar" call.
 * Cases marked "by the rule" apply the size rule those rows show to a string the table does
 * not have; the texts there follow Microsoft's page of ExpandEnvironmentStrings.
 */
#include <string.h>

#include "rt/test.h"
#include "rt/env.h"

static const char FILL[] = "abcdefghijklmnopqrstuv";

static void set(const char *name, const char *value)
{
    DWORD err = 1;
    T_TRUE(rt_env_set_a(name, value, &err));
    T_EQ(err, 0);
}

/* One call on a buffer filled as Wine's test fills it. */
static void expands(const char *src, DWORD size, DWORD want_ret, const char *want_buf)
{
    char buf[64];
    strcpy(buf, FILL);
    rt_set_last_error(T_CTX(), 0xdeadbeef);
    T_EQ(rt_env_expand_a(src, buf, size), want_ret);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);
    T_TRUE(strcmp(buf, want_buf) == 0);
    if (want_buf[0]) {                              /* written whole: nothing after the terminator moved */
        const size_t n = strlen(want_buf);
        T_TRUE(n + 1 >= sizeof FILL || memcmp(buf + n + 1, FILL + n + 1, sizeof FILL - n - 1) == 0);
    } else {                                        /* too small: only the first byte was cleared */
        T_TRUE(memcmp(buf + 1, FILL + 1, sizeof FILL - 1) == 0);
    }
}

/* A long result, in a buffer of its own. */
static DWORD expand_big(const char *src, char *buf, DWORD size) { return rt_env_expand_a(src, buf, size); }

TEST(core, env_expand)
{
    set("TVAR", "WINE");

    T_CASE("text without a reference: the size rule of the A form");
    expands("Long long value", 0, 17, "");
    expands("Long long value", 1, 17, "");
    expands("Long long value", 15, 17, "");
    expands("Long long value", 16, 17, "");         /* the text and its terminator are not enough */
    expands("Long long value", 17, 16, "Long long value");

    T_CASE("a variable that exists is replaced, once or twice");
    expands("%TVAR% long long", 0, 16, "");
    expands("%TVAR% long long", 5, 16, "");
    expands("%TVAR% long long", 15, 16, "");
    expands("%TVAR% long long", 16, 15, "WINE long long");
    expands("%TVAR%%TVAR% long", 14, 15, "");
    expands("%TVAR%%TVAR% long", 15, 14, "WINEWINE long");
    expands("%TVAR% %TVAR% long", 15, 16, "");
    expands("%TVAR% %TVAR% long", 16, 15, "WINE WINE long");

    T_CASE("a variable that does not exist, and a percent sign without a partner, stay as they are");
    expands("%TVAR2% long long", 1, 19, "");
    expands("%TVAR2% long long", 18, 19, "");
    expands("%TVAR2% long long", 19, 18, "%TVAR2% long long");
    expands("%TVAR long long", 16, 17, "");
    expands("%TVAR long long", 17, 16, "%TVAR long long");

    T_CASE("a value is not read again, and the name is found in any case");
    set("EnvVar", "Long long value");
    set("IndirectVar", "Foo%EnvVar%Bar");
    expands("Indirect-%IndirectVar%-Indirect", 40, 33, "Indirect-Foo%EnvVar%Bar-Indirect");
    expands("%tvar% and %Tvar%", 40, 14, "WINE and WINE");      /* by the rule; the page: case is ignored */

    T_CASE("by the rule: the empty string, two percent signs, an empty value");
    expands("", 2, 1, "");
    {
        char one[2] = { 'x', 'y' };
        T_EQ(rt_env_expand_a("", one, 1), 2);       /* too small by the rule: L + 2 */
        T_EQ(one[0], 0);
        T_EQ(one[1], 'y');
    }
    expands("100%% sure", 40, 11, "100%% sure");    /* "%%" names no variable */
    expands("50% off", 40, 8, "50% off");
    set("EMPTY", "");
    expands("[%EMPTY%]", 40, 3, "[]");
    expands("%NOPE% and %TVAR%", 40, 16, "%NOPE% and WINE");    /* " and " names no variable */
    expands("%NOPE%NOPE2%", 40, 13, "%NOPE%NOPE2%");

    T_CASE("no buffer and size 0 ask for the size and write nothing");
    rt_set_last_error(T_CTX(), 0xdeadbeef);
    T_EQ(rt_env_expand_a("%EnvVar%", NULL, 0), 17); /* Wine: 17 on "win2k, XP, win2k3" and later */
    T_EQ(rt_env_expand_a("%EnvVar%", NULL, 16), 17);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);

    T_CASE("a result up to 32767 bytes is given; a longer one is fatal");
    {
        static char big[20001], buf[40002], src[32770];
        DWORD err = 1;
        memset(big, 'v', 20000);
        big[20000] = 0;
        T_TRUE(rt_env_set_a("BIG", big, &err));
        T_EQ(expand_big("%BIG%", buf, sizeof buf), 20001);
        T_EQ(strlen(buf), 20000);
        T_TRUE(memcmp(buf, big, 20001) == 0);
        T_EQ(expand_big("%BIG%+12766+", buf, 0), 20000 + 7 + 2);
        memset(src, 's', 32767);
        memcpy(src, "%BIG%", 5);                    /* 20000 + 32762 bytes */
        src[32767] = 0;
        T_TRAPS(expand_big(src, buf, sizeof buf), "a result of more than 32767 bytes");
        T_TRAPS(expand_big("%BIG%%BIG%", buf, sizeof buf), "a result of more than 32767 bytes");
        memset(src, 's', 32768);
        src[32768] = 0;
        T_TRAPS(expand_big(src, buf, sizeof buf), "a source string of 32768 bytes");
        memset(src, 's', 32767);                    /* the longest source and result that are taken */
        src[32767] = 0;
        T_EQ(expand_big(src, buf, sizeof buf), 32768);
        T_TRUE(strcmp(buf, src) == 0);
    }

    T_CASE("what is not verified, and what an A function cannot carry, is fatal");
    {
        char buf[64];
        static const WCHAR name[] = { 'A', 'C', 'C', 'E', 'N', 'T', 0 }, value[] = { 'c', 'a', 'f', 0x00E9, 0 };
        DWORD err = 1;
        T_TRUE(rt_env_set_w(name, value, &err));
        T_TRAPS(rt_env_expand_a(NULL, buf, sizeof buf), "NULL source string");
        T_TRAPS(rt_env_expand_a("caf\xE9", buf, sizeof buf), "the source string has byte 0xE9");
        T_TRAPS(rt_env_expand_a("%ACCENT%", buf, sizeof buf), "the value of ACCENT has U+00E9");
        T_TRAPS(rt_env_expand_a("%NOPE%TVAR%", buf, sizeof buf), "whether Windows expands it is not verified");
        T_TRAPS(rt_env_expand_a("%%TVAR%", buf, sizeof buf), "followed by \"TVAR%\"");
        T_TRAPS(rt_env_expand_a("%TVAR%", NULL, 6), "NULL buffer of 6 bytes for a result of 4");
        expands("%TVAR% is not %ACCENT", 40, 20, "WINE is not %ACCENT");   /* the accent is never copied */
    }
}
