#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "rt/test.h"
#include "rt/path.h"

/* The core gives the codes of the failures (its own return for the same name is the
 * oracle).  The getter is this family's GetCurrentDirectoryW, which the test reads the
 * current directory through. */

#define BUFN  1024u       /* WCHARs in a test buffer */
#define FILL  0xCDCDu    /* what a buffer holds before a call */

/* Writes an ASCII name as WCHARs, its terminator, and FILL after it (wchar_t is not used). */
static void name(WCHAR *d, const char *s)
{
    unsigned i = 0;
    for (; s[i]; i++) d[i] = (WCHAR)(unsigned char)s[i];
    d[i] = 0;
    for (i++; i < BUFN; i++) d[i] = FILL;
}

/* The current directory as the guest's getter gives it. */
static DWORD now(WCHAR *got)
{
    for (unsigned i = 0; i < BUFN; i++) got[i] = FILL;
    return CALL(kernel32, GetCurrentDirectoryW, BUFN, got);
}

TEST(kernel32, SetCurrentDirectoryW)
{
    WCHAR start[BUFN], sub[BUFN], up[BUFN], missing[BUFN], file[BUFN], got[BUFN], want[BUFN];
    char host[1024];
    DWORD n, e, sl;
    FILE *f;

    T_CASE("sets an existing directory and returns non-zero");
    sl = rt_path_get_cwd_w(start, BUFN);            /* the directory at start-up: set again at the end */
    snprintf(host, sizeof host, "%s/sub", rt_test_install_root());
    T_TRUE(mkdir(host, 0777) == 0);
    rt_path_invalidate(rt_test_install_root());
    name(sub, "C:\\Titanfall2\\sub");
    T_EQ(CALL(kernel32, SetCurrentDirectoryW, sub), TRUE);
    n = now(got);
    name(want, "C:\\Titanfall2\\sub");
    T_EQ(n, 17u);
    T_TRUE(memcmp(got, want, (n + 1) * sizeof(WCHAR)) == 0);

    T_CASE("setting .. goes back up");
    name(up, "..");
    T_EQ(CALL(kernel32, SetCurrentDirectoryW, up), TRUE);
    n = now(got);
    name(want, "C:\\Titanfall2");
    T_EQ(n, 13u);
    T_TRUE(memcmp(got, want, (n + 1) * sizeof(WCHAR)) == 0);

    T_CASE("a directory that does not exist fails with the core's code, the directory stays");
    name(missing, "C:\\Titanfall2\\nope");
    e = rt_path_set_cwd_w(missing);
    T_TRUE(e != 0);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, SetCurrentDirectoryW, missing), FALSE);
    T_EQ(T_LAST_ERROR(), e);
    n = now(got);
    T_EQ(n, 13u);
    T_TRUE(memcmp(got, want, (n + 1) * sizeof(WCHAR)) == 0);

    T_CASE("a file fails with the core's code, the directory stays");
    snprintf(host, sizeof host, "%s/file.txt", rt_test_install_root());
    f = fopen(host, "w");
    T_TRUE(f != NULL);
    if (f) fclose(f);
    rt_path_invalidate(rt_test_install_root());
    name(file, "C:\\Titanfall2\\file.txt");
    e = rt_path_set_cwd_w(file);
    T_TRUE(e != 0);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, SetCurrentDirectoryW, file), FALSE);
    T_EQ(T_LAST_ERROR(), e);
    n = now(got);
    T_EQ(n, 13u);
    T_TRUE(memcmp(got, want, (n + 1) * sizeof(WCHAR)) == 0);

    T_CASE("NULL fails with the core's code, the directory stays");
    e = rt_path_set_cwd_w(NULL);
    T_TRUE(e != 0);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, SetCurrentDirectoryW, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), e);
    n = now(got);
    T_EQ(n, 13u);
    T_TRUE(memcmp(got, want, (n + 1) * sizeof(WCHAR)) == 0);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetCurrentDirectoryW, sub), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    /* Set the first directory again at the end. */
    T_EQ(CALL(kernel32, SetCurrentDirectoryW, start), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    n = now(got);
    T_EQ(n, sl);
    T_TRUE(memcmp(got, start, (n + 1) * sizeof(WCHAR)) == 0);
}
