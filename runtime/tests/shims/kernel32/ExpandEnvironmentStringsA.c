#include <string.h>

#include "rt/test.h"
#include "rt/env.h"

/* The variables are set through the core (rt_env_set_a, rt_env_set_w), as in
 * runtime/tests/shims/kernel32/GetEnvironmentVariableA.c; the names are unique to this test.
 * The values are Wine's test_ExpandEnvironmentStringsA values (see rt/env.h).  A buffer is preset
 * to PRESET, so a byte the call does not write still shows.  PRESET is 22 letters and a terminator. */
static const char preset[] = "abcdefghijklmnopqrstuv";

static void fill_preset(char *buf)
{
    memcpy(buf, preset, sizeof preset);
}

/* 1 if the bytes of buf from offset `from` to the end of the preset are still the preset's. */
static int preset_from(const char *buf, unsigned from)
{
    return memcmp(buf + from, preset + from, sizeof preset - from) == 0;
}

TEST(kernel32, ExpandEnvironmentStringsA)
{
    char buf[sizeof preset];
    static char longsrc[32769];     /* 32768 bytes of 'a' and a terminator */
    static char bigval[20001];      /* a value of 20000 bytes */
    static const WCHAR name_w[] = { 'T', 'N', 'A', 0 };
    static const WCHAR value_w[] = { 0x00E9, 0 };   /* a value with a character that is not ASCII */
    DWORD err = 0;

    T_TRUE(rt_env_set_a("TVAR", "WINE", &err));
    T_EQ(err, 0);

    T_CASE("expands a variable that is set and ignores the case of its name");
    fill_preset(buf);
    T_EQ(CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR% long long", buf, 16), 15);
    T_TRUE(memcmp(buf, "WINE long long", 15) == 0);     /* the text and its terminator */
    T_TRUE(preset_from(buf, 15));                      /* nothing written past the terminator */
    fill_preset(buf);
    T_EQ(CALL(kernel32, ExpandEnvironmentStringsA, "%tvar% long long", buf, 16), 15);
    T_TRUE(memcmp(buf, "WINE long long", 15) == 0);

    T_CASE("a buffer that holds the result gets it with its terminator and the length of the result plus one is returned");
    fill_preset(buf);
    T_EQ(CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR% long long", buf, sizeof buf), 15);
    T_TRUE(memcmp(buf, "WINE long long", 15) == 0);
    T_TRUE(preset_from(buf, 15));

    T_CASE("a name that is not set is copied as it is");
    fill_preset(buf);
    T_EQ(CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR2% long long", buf, 19), 18);
    T_TRUE(memcmp(buf, "%TVAR2% long long", 18) == 0);  /* the text and its terminator */
    T_TRUE(preset_from(buf, 18));

    T_CASE("a percent sign with no later percent sign is copied as it is");
    fill_preset(buf);
    T_EQ(CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR long long", buf, 17), 16);
    T_TRUE(memcmp(buf, "%TVAR long long", 16) == 0);
    T_TRUE(preset_from(buf, 16));

    T_CASE("a buffer too small or size 0 gets an empty string and the length plus two is returned");
    fill_preset(buf);
    T_EQ(CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR% long long", buf, 15), 16);
    T_EQ(buf[0], 0);
    T_TRUE(preset_from(buf, 1));                       /* only the first byte changed */
    fill_preset(buf);
    T_EQ(CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR% long long", buf, 0), 16);
    T_EQ(buf[0], 0);
    T_TRUE(preset_from(buf, 1));

    T_CASE("a NULL buffer with size 0 returns the length plus two");
    T_EQ(CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR% long long", NULL, 0), 16);

    T_CASE("does not change last error also when the buffer is too small");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    fill_preset(buf);
    CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR% long long", buf, 16);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);
    CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR% long long", buf, 15);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);
    CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR% long long", NULL, 0);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);

    /* The core stops (fatal) on each of these, for the input shown; the shim adds no guard.  Each
     * statement runs in a forked child, so the variables set here are seen by it. */
    memset(longsrc, 'a', sizeof longsrc - 1);
    longsrc[sizeof longsrc - 1] = 0;
    memset(bigval, 'x', sizeof bigval - 1);
    bigval[sizeof bigval - 1] = 0;
    T_TRUE(rt_env_set_w(name_w, value_w, &err));
    T_EQ(err, 0);
    T_TRUE(rt_env_set_a("TBIG", bigval, &err));
    T_EQ(err, 0);

    T_TRAPS(CALL(kernel32, ExpandEnvironmentStringsA, NULL, buf, 16),
            "ExpandEnvironmentStringsA: NULL source string");
    T_TRAPS(CALL(kernel32, ExpandEnvironmentStringsA, "caf\xE9", buf, 16),
            "the source string has byte 0xE9");
    T_TRAPS(CALL(kernel32, ExpandEnvironmentStringsA, "%TNA%", buf, 16),
            "has U+00E9, which is not ASCII");
    T_TRAPS(CALL(kernel32, ExpandEnvironmentStringsA, "%NOPE%TVAR%", buf, 16),
            "followed by \"TVAR%\"");
    T_TRAPS(CALL(kernel32, ExpandEnvironmentStringsA, "%TVAR% long long", NULL, 16),
            "NULL buffer of 16 bytes");
    T_TRAPS(CALL(kernel32, ExpandEnvironmentStringsA, longsrc, buf, 16),
            "a source string of 32768 bytes");
    T_TRAPS(CALL(kernel32, ExpandEnvironmentStringsA, "%TBIG%%TBIG%", buf, 16),
            "a result of more than 32767 bytes");
}
