#include <sys/stat.h>
#include "rt/test.h"
#include "rt/path.h"
#include "rt/locale.h"

/* The n bytes at s set to c. */
static void fill_bytes(char *s, char c, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        s[i] = c;
}

/* A string of n copies of c, NUL-terminated; s must hold n + 1 chars. */
static void fill_name(char *s, char c, unsigned n)
{
    fill_bytes(s, c, n);
    s[n] = 0;
}

/* Copies the NUL-terminated s to d and returns the place of the NUL in d. */
static char *put(char *d, const char *s)
{
    while ((*d = *s++) != 0)
        d++;
    return d;
}

/* True if the n bytes at a are the n bytes at b. */
static int same_bytes(const char *a, const char *b, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

/* True if the n bytes at s are all c. */
static int all_bytes(const char *s, char c, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        if (s[i] != c)
            return 0;
    return 1;
}

TEST(kernel32, GetFullPathNameA)
{
    char buf[MAX_PATH + 16];
    char name[MAX_PATH + 1];
    char longname[320];
    char rootname[320];
    char want[MAX_PATH + 16];
    char cwd_a[MAX_PATH];
    char host[1024];
    WCHAR cwd[MAX_PATH];
    static const WCHAR greek[] = { 0x03A9, 0 };                   /* U+03A9 is not in code page 1252 */
    LPSTR part;
    DWORD cn, n, wl;
    BOOL used = FALSE;
    char *p;
    int conv;

    T_CASE("a relative name gives the current directory, a separator and the name, and the part points at the name");
    cn = rt_path_get_cwd_w(cwd, MAX_PATH);
    T_REQUIRE(cn + 8 < MAX_PATH);
    conv = rt_wide_to_mb(CP_ACP, WC_NO_BEST_FIT_CHARS, cwd, (int)cn, cwd_a, MAX_PATH, NULL, &used);
    T_REQUIRE(conv == (int)cn && !used);
    cwd_a[cn] = 0;
    p = put(want, cwd_a);
    p = put(p, "\\x.txt");
    wl = (DWORD)(p - want);
    fill_bytes(buf, 'Z', sizeof buf);
    part = NULL;
    n = CALL(kernel32, GetFullPathNameA, "x.txt", sizeof buf, buf, &part);
    T_EQ(n, wl);
    T_TRUE(same_bytes(buf, want, wl + 1));
    T_TRUE(all_bytes(buf + wl + 1, 'Z', sizeof buf - wl - 1));
    T_TRUE(part == buf + cn + 1);

    T_CASE("a name with dot-dot is normalised and the part points at its last component");
    fill_bytes(buf, 'Z', sizeof buf);
    part = NULL;
    n = CALL(kernel32, GetFullPathNameA, "C:\\a\\..\\b\\c.bin", sizeof buf, buf, &part);
    T_EQ(n, 10);
    T_TRUE(same_bytes(buf, "C:\\b\\c.bin", 11));
    T_TRUE(all_bytes(buf + 11, 'Z', sizeof buf - 11));
    T_TRUE(part == buf + 5);

    T_CASE("a name that ends in a separator gives a NULL part");
    fill_bytes(buf, 'Z', sizeof buf);
    part = buf + 3;
    n = CALL(kernel32, GetFullPathNameA, "C:\\a\\", sizeof buf, buf, &part);
    T_EQ(n, 5);
    T_TRUE(same_bytes(buf, "C:\\a\\", 6));
    T_TRUE(part == NULL);

    T_CASE("a code page 1252 character comes back as the same byte");
    fill_bytes(buf, 'Z', sizeof buf);
    n = CALL(kernel32, GetFullPathNameA, "C:\\caf\xe9.txt", sizeof buf, buf, NULL);
    T_EQ(n, 11);
    T_TRUE(same_bytes(buf, "C:\\caf\xe9.txt", 12));
    T_TRUE(all_bytes(buf + 12, 'Z', sizeof buf - 12));

    T_CASE("lpFilePart NULL is accepted: the length is returned and the buffer is written");
    fill_bytes(buf, 'Z', sizeof buf);
    n = CALL(kernel32, GetFullPathNameA, "C:\\a\\..\\b\\c.bin", sizeof buf, buf, NULL);
    T_EQ(n, 10);
    T_TRUE(same_bytes(buf, "C:\\b\\c.bin", 11));
    T_TRUE(all_bytes(buf + 11, 'Z', sizeof buf - 11));

    T_CASE("a name of 259 characters and a result of 259 characters are accepted");
    put(name, "C:\\");
    fill_name(name + 3, 'n', 256);                         /* 259 characters in all */
    fill_bytes(buf, 'Z', sizeof buf);
    part = NULL;
    n = CALL(kernel32, GetFullPathNameA, name, MAX_PATH, buf, &part);
    T_EQ(n, 259);
    T_TRUE(same_bytes(buf, name, 260));
    T_TRUE(all_bytes(buf + 260, 'Z', sizeof buf - 260));
    T_TRUE(part == buf + 3);

    T_CASE("the size query with a zero buffer returns the length plus one and writes nothing");
    fill_bytes(buf, 'Z', sizeof buf);
    part = buf + 3;
    n = CALL(kernel32, GetFullPathNameA, "x.txt", 0, NULL, NULL);
    T_EQ(n, wl + 1);
    n = CALL(kernel32, GetFullPathNameA, "x.txt", 0, buf, &part);
    T_EQ(n, wl + 1);
    T_TRUE(all_bytes(buf, 'Z', sizeof buf));
    T_TRUE(part == buf + 3);

    T_CASE("a buffer one byte too small returns the length plus one and writes neither the buffer nor the part");
    fill_bytes(buf, 'Z', sizeof buf);
    part = buf + 3;
    n = CALL(kernel32, GetFullPathNameA, "x.txt", wl, buf, &part);
    T_EQ(n, wl + 1);
    T_TRUE(all_bytes(buf, 'Z', sizeof buf));
    T_TRUE(part == buf + 3);

    T_CASE("NULL, an empty name and a name of spaces only fail with ERROR_INVALID_NAME and write nothing");
    fill_bytes(buf, 'Z', sizeof buf);
    part = buf + 3;
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, GetFullPathNameA, NULL, sizeof buf, buf, &part);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_NAME);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, GetFullPathNameA, "", sizeof buf, buf, &part);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_NAME);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, GetFullPathNameA, "   ", sizeof buf, buf, &part);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_NAME);
    T_TRUE(all_bytes(buf, 'Z', sizeof buf));
    T_TRUE(part == buf + 3);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, GetFullPathNameA, "C:\\a\\b", sizeof buf, buf, &part);
    T_EQ(n, 6);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, GetFullPathNameA, "x.txt", sizeof buf, buf, NULL);
    T_EQ(n, wl);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stops.  Each proves an RT-TRAPS entry; they come after the checks of the last case. */
    fill_name(longname, 'n', 300);
    T_TRAPS(CALL(kernel32, GetFullPathNameA, longname, sizeof buf, buf, &part), "a name of MAX_PATH characters or more");
    fill_name(longname, 'n', MAX_PATH);
    T_TRAPS(CALL(kernel32, GetFullPathNameA, longname, sizeof buf, buf, &part), "a name of MAX_PATH characters or more");
    rootname[0] = '\\';
    fill_name(rootname + 1, 'x', 258);                      /* "C:\" and 258 characters: 261 */
    T_TRAPS(CALL(kernel32, GetFullPathNameA, rootname, sizeof buf, buf, &part), "a result of MAX_PATH characters or more");
    T_TRAPS(CALL(kernel32, GetFullPathNameA, "x.txt", sizeof buf, NULL, NULL), "lpBuffer NULL with room for the result");
    T_TRAPS(CALL(kernel32, GetFullPathNameA, "\\\\.", sizeof buf, buf, &part), "with nothing after it");
    T_TRAPS(CALL(kernel32, GetFullPathNameA, "\\\\?", sizeof buf, buf, &part), "with nothing after it");
    T_TRAPS(CALL(kernel32, GetFullPathNameA, "\\\\", sizeof buf, buf, &part), "a UNC path without a server name");
    T_TRAPS(CALL(kernel32, GetFullPathNameA, "\\\\\\x", sizeof buf, buf, &part), "a UNC path without a server name");
    T_TRAPS(CALL(kernel32, GetFullPathNameA, "C:\\dir\\nul.txt", sizeof buf, buf, &part), "a DOS device name followed by '.' or ':'");
    T_TRAPS(CALL(kernel32, GetFullPathNameA, "C:\\dir\\NUL:", sizeof buf, buf, &part), "a DOS device name followed by '.' or ':'");
    /* The current directory is set last.  The test starts in the install root (the core's own cwd
     * test relies on that too), and the new directory must exist, so the test makes <U+03A9> there
     * and sets it as the current directory with a relative name: a name outside code page 1252 in
     * the current directory is then refused. */
    p = put(host, rt_test_install_root());
    p = put(p, "/");
    put(p, "\xce\xa9");
    mkdir(host, 0755);
    rt_path_invalidate(rt_test_install_root());            /* made outside the runtime: drop its listing (RUNTIME.md 7.2) */
    T_EQ(rt_path_set_cwd_w(greek), 0);
    T_TRAPS(CALL(kernel32, GetFullPathNameA, "x.txt", sizeof buf, buf, &part), "a character outside code page 1252 in the result");
}
