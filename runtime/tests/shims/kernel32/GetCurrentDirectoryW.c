#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "rt/test.h"
#include "rt/path.h"

/* The core is the oracle for the directory at start-up and for what a too-small buffer holds.
 * The sub-directory is made on the host under the install root and set through the core;
 * the getter (this shim) is then checked against the names the test wrote itself. */

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

static void fill(WCHAR *b)
{
    for (unsigned i = 0; i < BUFN; i++) b[i] = FILL;
}

TEST(kernel32, GetCurrentDirectoryW)
{
    WCHAR want[BUFN], got[BUFN], small[BUFN], core[BUFN], expect[BUFN], sub[BUFN];
    char host[1024];
    DWORD wl, n;

    T_CASE("returns the length of the current directory, without the terminator");
    fill(want);
    fill(got);
    wl = rt_path_get_cwd_w(want, BUFN);                 /* the oracle */
    name(expect, "C:\\Titanfall2");                     /* the directory at start-up (rt/path.h) */
    T_EQ(wl, 13u);
    T_TRUE(memcmp(want, expect, (wl + 1) * sizeof(WCHAR)) == 0);
    n = CALL(kernel32, GetCurrentDirectoryW, BUFN, got);
    T_EQ(n, wl);
    T_TRUE(memcmp(got, want, (n + 1) * sizeof(WCHAR)) == 0);   /* the text and its terminator */
    T_EQ(got[n + 1], FILL);                             /* nothing written past the terminator */

    T_CASE("size query and a buffer one WCHAR too small return the length plus one");
    T_EQ(CALL(kernel32, GetCurrentDirectoryW, 0, NULL), wl + 1);
    fill(small);
    T_EQ(CALL(kernel32, GetCurrentDirectoryW, wl, small), wl + 1);

    T_CASE("a buffer one WCHAR too small holds what the core leaves in it");
    fill(small);
    n = CALL(kernel32, GetCurrentDirectoryW, wl, small);
    fill(core);
    T_EQ(rt_path_get_cwd_w(core, wl), n);               /* the core's own return */
    T_TRUE(memcmp(small, core, BUFN * sizeof(WCHAR)) == 0);

    T_CASE("gives a directory that was set, without a trailing backslash");
    snprintf(host, sizeof host, "%s/sub", rt_test_install_root());
    T_TRUE(mkdir(host, 0777) == 0);
    rt_path_invalidate(rt_test_install_root());
    name(sub, "C:\\Titanfall2\\sub");
    T_EQ(rt_path_set_cwd_w(sub), 0);
    fill(got);
    n = CALL(kernel32, GetCurrentDirectoryW, BUFN, got);
    name(expect, "C:\\Titanfall2\\sub");
    T_EQ(n, 17u);                                       /* 17 WCHARs: no backslash after "sub" */
    T_TRUE(memcmp(got, expect, (n + 1) * sizeof(WCHAR)) == 0);

    T_CASE("setting .. goes back up");
    name(sub, "..");
    T_EQ(rt_path_set_cwd_w(sub), 0);
    fill(got);
    n = CALL(kernel32, GetCurrentDirectoryW, BUFN, got);
    name(expect, "C:\\Titanfall2");
    T_EQ(n, 13u);
    T_TRUE(memcmp(got, expect, (n + 1) * sizeof(WCHAR)) == 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetCurrentDirectoryW, BUFN, got);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, GetCurrentDirectoryW, 0, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, GetCurrentDirectoryW, 1, small);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRAPS(CALL(kernel32, GetCurrentDirectoryW, 5, NULL), "NULL buffer with a nonzero length");
}
