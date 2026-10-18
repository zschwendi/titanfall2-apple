#include "rt/test.h"
#include "rt/env.h"

/* A string of the block, read as the guest reads it: the number of CHARs before its NUL, or
 * -1 if there is no NUL within 65536 CHARs. */
static int anul(const CHAR *p)
{
    int n = 0;
    while (p[n]) {
        if (++n > 65536) return -1;
    }
    return n;
}

/* Does the NUL-terminated string p equal the string s? */
static int ascii_is(const CHAR *p, const char *s)
{
    for (; *s; s++, p++) {
        if (*p != *s) return 0;
    }
    return *p == 0;
}

/* Walks the block: NAME=value strings, each NUL-terminated, then an empty string (the block's
 * second NUL).  Returns the number of strings, or -1 if a string has no '=' after its first
 * character, or if the block does not end in an empty string within a sane bound.  *found is
 * set to 1 if one string equals want (NULL: nothing is looked for). */
static int block_walk(const CHAR *block, const char *want, int *found)
{
    const CHAR *p = block;
    int n = 0;

    *found = 0;
    for (;;) {
        int len = anul(p);
        int eq = 1;

        if (len < 0 || n > 100000) return -1;
        if (len == 0) return n;                 /* the empty string ends the block */
        while (eq < len && p[eq] != '=') eq++;
        if (eq == len) return -1;               /* no '=' after the first character */
        if (want && ascii_is(p, want)) *found = 1;
        p += len + 1;
        n++;
    }
}

static CHAR vbuf[RT_ENV_VALUE_MAX + 1];

/* Checks every string of the block against the core's own view of the variable it names:
 * rt_env_get_a finds the name, and returns the characters that the block holds after its '='.
 * Returns the number of strings that disagree (0 when the whole block is right). */
static int block_values_agree(const CHAR *block)
{
    const CHAR *p = block;
    int bad = 0;

    for (;;) {
        int len = anul(p);
        int eq = 1, vlen, i;
        CHAR name[256];
        DWORD err = 0, n;

        if (len < 0) return bad + 1;            /* no NUL: not a block */
        if (len == 0) return bad;               /* the empty string ends the block */
        while (eq < len && p[eq] != '=') eq++;  /* block_walk has checked there is one */
        if (eq >= 255 || eq == len) {
            bad++;
        } else {
            for (i = 0; i < eq; i++) name[i] = p[i];
            name[eq] = 0;
            vlen = len - eq - 1;
            n = rt_env_get_a(name, vbuf, RT_ENV_VALUE_MAX + 1, &err);
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

/* Sets a variable whose value has a character >= 0x80 (rt_env_set_w accepts it), then asks for
 * a block: the core STOPS there.  Run in a forked child by T_TRAPS. */
static void block_with_non_ascii(void)
{
    static const WCHAR name[] = { 'R', 'T', '_', 'T', '_', 'U', 0 };
    static const WCHAR value[] = { 0x00E9, 0 };
    DWORD err = 0;

    if (!rt_env_set_w(name, value, &err)) return;
    CALL(kernel32, GetEnvironmentStrings);
}

/* Each case takes its own block and frees it once, with the core's release. */
TEST(kernel32, GetEnvironmentStrings)
{
    CHAR *block, *b1, *b2;
    CHAR buf[64];
    DWORD err = 0;
    int n, n0, n1, found, f0, f1, f2;

    T_CASE("returns a block of NAME=value strings, each NUL-terminated, ended by a second NUL");
    block = CALL(kernel32, GetEnvironmentStrings);
    T_TRUE(block != NULL);
    n = block_walk(block, NULL, &found);
    T_TRUE(n >= 1);
    T_EQ(block_values_agree(block), 0);         /* every string's value is the variable's value */
    T_EQ(rt_env_block_free(block), TRUE);

    T_CASE("holds a variable set with rt_env_set_a");
    T_EQ(rt_env_set_a("RT_T_A", "one", &err), TRUE);
    block = CALL(kernel32, GetEnvironmentStrings);
    T_TRUE(block != NULL);
    n = block_walk(block, "RT_T_A=one", &found);
    T_TRUE(n >= 1);
    T_EQ(found, 1);                             /* RT_T_A=one is one of the strings */
    T_EQ(rt_env_block_free(block), TRUE);

    T_CASE("two calls give two different blocks");
    b1 = CALL(kernel32, GetEnvironmentStrings);
    b2 = CALL(kernel32, GetEnvironmentStrings);
    T_TRUE(b1 != NULL);
    T_TRUE(b2 != NULL);
    T_TRUE(b1 != b2);
    n = block_walk(b1, NULL, &found);
    T_TRUE(n >= 1);
    n = block_walk(b2, NULL, &found);
    T_TRUE(n >= 1);
    T_EQ(rt_env_block_free(b1), TRUE);
    T_EQ(rt_env_block_free(b2), TRUE);

    T_CASE("is a snapshot: later changes do not show in it");
    T_EQ(rt_env_set_a("RT_T_A", "one", &err), TRUE);
    block = CALL(kernel32, GetEnvironmentStrings);
    T_TRUE(block != NULL);
    n0 = block_walk(block, "RT_T_A=one", &f0);
    T_TRUE(n0 >= 1);
    T_EQ(f0, 1);
    T_EQ(rt_env_set_a("RT_T_NEW", "after", &err), TRUE);   /* a new variable: the change is real */
    T_EQ(rt_env_get_a("RT_T_NEW", buf, sizeof buf, &err), 5);
    T_EQ(rt_env_set_a("RT_T_A", "two", &err), TRUE);       /* an existing variable: the change is real */
    n1 = block_walk(block, "RT_T_NEW=after", &f1);
    T_EQ(n1, n0);                               /* the new variable is not in the block */
    T_EQ(f1, 0);
    T_EQ(block_walk(block, "RT_T_A=one", &f2), n0);
    T_EQ(f2, 1);                                /* the value it had is still in the block */
    T_EQ(rt_env_block_free(block), TRUE);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    block = CALL(kernel32, GetEnvironmentStrings);
    T_TRUE(block != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_env_block_free(block), TRUE);

    /* The stop: after the checks of the last case.  The report names the variable. */
    T_TRAPS(block_with_non_ascii(), "GetEnvironmentStringsA: variable");
}
