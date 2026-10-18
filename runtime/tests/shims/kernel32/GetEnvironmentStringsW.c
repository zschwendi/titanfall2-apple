#include "rt/test.h"
#include "rt/env.h"

/* A string of the block, read as the guest reads it: the number of WCHARs before its NUL, or
 * -1 if there is no NUL within 65536 WCHARs. */
static int wnul(const WCHAR *p)
{
    int n = 0;
    while (p[n]) {
        if (++n > 65536) return -1;
    }
    return n;
}

/* Does the NUL-terminated WCHAR string p equal the ASCII string s? */
static int wide_is(const WCHAR *p, const char *s)
{
    for (; *s; s++, p++) {
        if (*p != (unsigned char)*s) return 0;
    }
    return *p == 0;
}

/* Walks the block: NAME=value strings, each NUL-terminated, then an empty string (the block's
 * second NUL).  Returns the number of strings, or -1 if a string has no name before its '=',
 * or has no '=', or if the block does not end in an empty string within a sane bound.
 * *sysroot is set to 1 if one string is exactly SystemRoot=C:\Windows. */
static int block_walk(const WCHAR *block, int *sysroot)
{
    const WCHAR *p = block;
    int n = 0;

    *sysroot = 0;
    for (;;) {
        int len = wnul(p);
        int eq = 0;

        if (len < 0 || n > 100000) return -1;
        if (len == 0) return n;                 /* the empty string ends the block */
        while (eq < len && p[eq] != '=') eq++;
        if (eq == 0 || eq == len) return -1;    /* no name, or no '=' */
        if (wide_is(p, "SystemRoot=C:\\Windows")) *sysroot = 1;
        p += len + 1;
        n++;
    }
}

static WCHAR vbuf[RT_ENV_VALUE_MAX + 1];

/* Checks every string of the block against the core's own view of the variable it names:
 * rt_env_get_w finds the name, and returns the characters that the block holds after its '='.
 * Returns the number of strings that disagree (0 when the whole block is right). */
static int block_values_agree(const WCHAR *block)
{
    const WCHAR *p = block;
    int bad = 0;

    for (;;) {
        int len = wnul(p);
        int eq = 0, vlen, i;
        WCHAR name[256];
        DWORD err = 0, n;

        if (len < 0) return bad + 1;            /* no NUL: not a block */
        if (len == 0) return bad;               /* the empty string ends the block */
        while (eq < len && p[eq] != '=') eq++;  /* block_walk has checked there is one */
        if (eq >= 255) {
            bad++;
        } else {
            for (i = 0; i < eq; i++) name[i] = p[i];
            name[eq] = 0;
            vlen = len - eq - 1;
            n = rt_env_get_w(name, vbuf, RT_ENV_VALUE_MAX + 1, &err);
            if (err || n != (DWORD)vlen) {
                bad++;
            } else {
                for (i = 0; i < vlen; i++) {
                    if (vbuf[i] != p[eq + 1 + i]) { bad++; break; }
                }
            }
        }
        p += len + 1;
    }
}

/* Copies the ASCII string s into dst as WCHARs, with its terminator. */
static void to_w(WCHAR *dst, const char *s)
{
    while (*s) {
        *dst++ = (unsigned char)*s++;
    }
    *dst = 0;
}

/* Each case takes its own block and frees it once, with the core's release. */
TEST(kernel32, GetEnvironmentStringsW)
{
    WCHAR *block;
    WCHAR name[64], value[64], buf[64];
    DWORD err = 0;
    int n, n0, n1, sys, sys0, sys1;

    T_CASE("returns a block of NAME=value strings, each NUL-terminated, ended by a second NUL");
    block = CALL(kernel32, GetEnvironmentStringsW);
    T_TRUE(block != NULL);
    n = block_walk(block, &sys);
    T_TRUE(n >= 1);
    T_EQ(block_values_agree(block), 0);         /* every string's value is the variable's value */
    T_EQ(rt_env_block_free(block), TRUE);

    T_CASE("holds SystemRoot");
    block = CALL(kernel32, GetEnvironmentStringsW);
    T_TRUE(block != NULL);
    n = block_walk(block, &sys);
    T_TRUE(n >= 1);
    T_EQ(sys, 1);                               /* SystemRoot=C:\Windows is one of the strings */
    T_EQ(rt_env_block_free(block), TRUE);

    T_CASE("is a snapshot: later changes do not show in it");
    block = CALL(kernel32, GetEnvironmentStringsW);
    T_TRUE(block != NULL);
    n0 = block_walk(block, &sys0);
    T_TRUE(n0 >= 1);
    T_EQ(sys0, 1);
    to_w(name, "RT_SNAPSHOT_TEST");
    to_w(value, "after");
    T_EQ(rt_env_set_w(name, value, &err), TRUE);     /* a new variable: the change is real */
    T_EQ(rt_env_get_w(name, buf, 64, &err), 5);
    to_w(name, "SystemRoot");
    to_w(value, "D:\\Other");
    T_EQ(rt_env_set_w(name, value, &err), TRUE);     /* an existing variable: the change is real */
    n1 = block_walk(block, &sys1);
    T_EQ(n1, n0);                               /* the new variable is not in the block */
    T_EQ(sys1, 1);                              /* SystemRoot in the block is still C:\Windows */
    T_EQ(rt_env_block_free(block), TRUE);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    block = CALL(kernel32, GetEnvironmentStringsW);
    T_TRUE(block != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_env_block_free(block), TRUE);
}
