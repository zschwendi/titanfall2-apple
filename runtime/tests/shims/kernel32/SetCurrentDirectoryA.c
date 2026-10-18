#include <stdio.h>
#include <sys/stat.h>
#include "rt/test.h"
#include "rt/path.h"
#include "rt/locale.h"

#define WIDE_CAP 400

/* s[0..n-1] = c; not terminated. */
static void fill_chars(char *s, char c, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        s[i] = c;
}

/* Makes the host directory <install root>/<rel>: the guest's C:\Titanfall2\<rel>. */
static void make_dir(const char *rel)
{
    char p[1024];

    snprintf(p, sizeof p, "%s/%s", rt_test_install_root(), rel);
    T_EQ(mkdir(p, 0755), 0);
}

static DWORD text_len(const char *s)
{
    DWORD n = 0;

    while (s[n])
        n++;
    return n;
}

/* 1 if the terminated WCHAR string w is the ASCII string s.  Reads no further than s does. */
static int wide_is(const WCHAR *w, const char *s)
{
    while (*s && *w == (unsigned char)*s) {
        s++;
        w++;
    }
    return *w == 0 && *s == 0;
}

TEST(kernel32, SetCurrentDirectoryA)
{
    WCHAR wide[WIDE_CAP];
    char long_name[301];
    DWORD e, e2, n;
    BOOL got;

    T_CASE("a sub-directory is set and read back");
    make_dir("sub-a");
    got = CALL(kernel32, SetCurrentDirectoryA, "C:\\Titanfall2\\sub-a");
    T_EQ(got, TRUE);
    n = rt_path_get_cwd_w(wide, MAX_PATH);
    T_EQ(n, text_len("C:\\Titanfall2\\sub-a"));
    T_EQ(wide[n], 0);
    T_TRUE(wide_is(wide, "C:\\Titanfall2\\sub-a"));

    T_CASE("a directory that does not exist, and NULL, fail with the code the core gives for the same name");
    T_REQUIRE(rt_mb_to_wide(CP_ACP, 0, "C:\\Titanfall2\\no-such-dir", -1, wide, WIDE_CAP) != 0);
    e = rt_path_set_cwd_w(wide);
    T_TRUE(e != 0);
    CALL(kernel32, SetLastError, 0x1234);
    got = CALL(kernel32, SetCurrentDirectoryA, "C:\\Titanfall2\\no-such-dir");
    T_EQ(got, FALSE);
    T_EQ(T_LAST_ERROR(), e);
    e2 = rt_path_set_cwd_w(NULL);
    T_TRUE(e2 != 0);
    CALL(kernel32, SetLastError, 0x1234);
    got = CALL(kernel32, SetCurrentDirectoryA, NULL);
    T_EQ(got, FALSE);
    T_EQ(T_LAST_ERROR(), e2);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    got = CALL(kernel32, SetCurrentDirectoryA, "C:\\Titanfall2\\sub-a");
    T_EQ(got, TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stops, each after the checks of the last case.  Each runs in a child process. */
    fill_chars(long_name, 'n', 300);
    long_name[300] = 0;
    T_TRAPS(CALL(kernel32, SetCurrentDirectoryA, long_name), "MAX_PATH characters or more");
    T_TRAPS(CALL(kernel32, SetCurrentDirectoryA, ""), "empty name");

    /* The first directory set in this test, set again at the end. */
    got = CALL(kernel32, SetCurrentDirectoryA, "C:\\Titanfall2\\sub-a");
    T_EQ(got, TRUE);
}
