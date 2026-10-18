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

/* Every byte of the buffer is the preset 'x'. */
static void preset(char *b, unsigned n)
{
    fill_chars(b, 'x', n);
}

/* Bytes from `from` up to `to` (not included) of b are still the preset 'x'. */
static void check_untouched(const char *b, unsigned from, unsigned to)
{
    unsigned i;

    for (i = from; i < to; i++)
        T_EQ((unsigned char)b[i], 'x');
}

/* The n characters of the directory, then its terminator, are in buf. */
static void check_dir_bytes(const char *buf, const WCHAR *wide, DWORD n)
{
    DWORD i;

    for (i = 0; i < n; i++)
        T_EQ((unsigned char)buf[i], wide[i]);
    T_EQ((unsigned char)buf[n], 0);
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

/* 1 if the NUL-terminated byte string a is the same as b. */
static int same_text(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

TEST(kernel32, GetCurrentDirectoryA)
{
    WCHAR wide[WIDE_CAP];
    char narrow[WIDE_CAP];
    char buf[MAX_PATH + 8];
    char seg1[101], seg2[101], seg3[51], rel[320];
    DWORD n, got, e;

    T_CASE("the directory at the start agrees with rt_path_get_cwd_w");
    n = rt_path_get_cwd_w(wide, MAX_PATH);
    T_REQUIRE(n > 0 && n < MAX_PATH);
    preset(buf, sizeof buf);
    got = CALL(kernel32, GetCurrentDirectoryA, MAX_PATH, buf);
    T_EQ(got, n);
    check_dir_bytes(buf, wide, n);
    check_untouched(buf, n + 1, (unsigned)sizeof buf);

    T_CASE("a size query, with length 0, gives the length with the terminator and writes nothing");
    preset(buf, sizeof buf);
    got = CALL(kernel32, GetCurrentDirectoryA, 0, NULL);
    T_EQ(got, n + 1);
    got = CALL(kernel32, GetCurrentDirectoryA, 0, buf);
    T_EQ(got, n + 1);
    check_untouched(buf, 0, (unsigned)sizeof buf);

    T_CASE("a buffer one byte too small gets an empty string and the length it needs");
    preset(buf, sizeof buf);
    got = CALL(kernel32, GetCurrentDirectoryA, n, buf);
    T_EQ(got, n + 1);
    T_EQ((unsigned char)buf[0], 0);
    check_untouched(buf, 1, (unsigned)sizeof buf);

    T_CASE("a buffer that holds the directory gets it with its terminator and the length is returned");
    preset(buf, sizeof buf);
    got = CALL(kernel32, GetCurrentDirectoryA, n + 1, buf);
    T_EQ(got, n);
    check_dir_bytes(buf, wide, n);
    check_untouched(buf, n + 1, (unsigned)sizeof buf);
    preset(buf, sizeof buf);
    got = CALL(kernel32, GetCurrentDirectoryA, MAX_PATH, buf);
    T_EQ(got, n);
    check_dir_bytes(buf, wide, n);
    check_untouched(buf, n + 1, (unsigned)sizeof buf);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    got = CALL(kernel32, GetCurrentDirectoryA, 0, NULL);
    T_EQ(got, n + 1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    got = CALL(kernel32, GetCurrentDirectoryA, MAX_PATH, buf);
    T_EQ(got, n);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a sub-directory set with rt_path_set_cwd_w is read back");
    make_dir("sub-a");
    T_REQUIRE(rt_mb_to_wide(CP_ACP, 0, "C:\\Titanfall2\\sub-a", -1, wide, WIDE_CAP) != 0);
    e = rt_path_set_cwd_w(wide);
    T_EQ(e, 0);
    preset(buf, sizeof buf);
    got = CALL(kernel32, GetCurrentDirectoryA, MAX_PATH, buf);
    T_EQ(got, text_len("C:\\Titanfall2\\sub-a"));
    T_TRUE(same_text(buf, "C:\\Titanfall2\\sub-a"));

    /* The stops, each after the checks of the last case.  Each runs in a child process. */
    T_TRAPS(CALL(kernel32, GetCurrentDirectoryA, MAX_PATH, NULL), "NULL buffer");

    /* A current directory of 266 characters: three nested directories of 100, 100 and 50. */
    fill_chars(seg1, 'a', 100);
    seg1[100] = 0;
    fill_chars(seg2, 'b', 100);
    seg2[100] = 0;
    fill_chars(seg3, 'c', 50);
    seg3[50] = 0;
    make_dir(seg1);
    snprintf(rel, sizeof rel, "%s/%s", seg1, seg2);
    make_dir(rel);
    snprintf(rel, sizeof rel, "%s/%s/%s", seg1, seg2, seg3);
    make_dir(rel);
    snprintf(narrow, sizeof narrow, "C:\\Titanfall2\\%s\\%s\\%s", seg1, seg2, seg3);
    T_REQUIRE(rt_mb_to_wide(CP_ACP, 0, narrow, -1, wide, WIDE_CAP) != 0);
    e = rt_path_set_cwd_w(wide);
    T_EQ(e, 0);
    T_TRAPS(CALL(kernel32, GetCurrentDirectoryA, MAX_PATH, buf), "MAX_PATH characters or more");

    /* A directory whose name is U+4E2D (UTF-8 E4 B8 AD). */
    make_dir("\xE4\xB8\xAD");
    T_REQUIRE(rt_mb_to_wide(CP_UTF8, 0, "C:\\Titanfall2\\\xE4\xB8\xAD", -1, wide, WIDE_CAP) != 0);
    e = rt_path_set_cwd_w(wide);
    T_EQ(e, 0);
    T_TRAPS(CALL(kernel32, GetCurrentDirectoryA, MAX_PATH, buf), "outside code page 1252");

    /* The first directory set in this test, set again at the end. */
    T_REQUIRE(rt_mb_to_wide(CP_ACP, 0, "C:\\Titanfall2\\sub-a", -1, wide, WIDE_CAP) != 0);
    e = rt_path_set_cwd_w(wide);
    T_EQ(e, 0);
}
