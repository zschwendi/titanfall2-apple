#include "rt/test.h"
#include "rt/path.h"

/* The n WCHARs at s set to c. */
static void fill_w(WCHAR *s, WCHAR c, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        s[i] = c;
}

/* True if the n WCHARs at a are the n WCHARs at b. */
static int same_w(const WCHAR *a, const WCHAR *b, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

/* True if the n WCHARs at s are all c. */
static int all_w(const WCHAR *s, WCHAR c, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        if (s[i] != c)
            return 0;
    return 1;
}

/* True if s holds the ASCII string a, NUL-terminated (the terminator is checked too). */
static int is_w(const WCHAR *s, const char *a)
{
    unsigned i;

    for (i = 0; ; i++) {
        if (s[i] != (unsigned char)a[i])
            return 0;
        if (a[i] == 0)
            return 1;
    }
}

/* Copies the n WCHARs at s to d. */
static void copy_w(WCHAR *d, const WCHAR *s, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        d[i] = s[i];
}

/* Copies the ASCII string s to d as WCHARs, NUL-terminated; returns the place of the NUL in d. */
static WCHAR *put(WCHAR *d, const char *s)
{
    while ((*d = (unsigned char)*s++) != 0)
        d++;
    return d;
}

/* The ASCII string a as a WCHAR string, for a call.  Eight slots, used in turn. */
static const WCHAR *W(const char *a)
{
    static WCHAR slot[8][512];
    static unsigned k;
    WCHAR *b = slot[k++ & 7];

    put(b, a);
    return b;
}

#define NW  300          /* the size of the result buffers, in WCHARs */

TEST(kernel32, GetFullPathNameW)
{
    WCHAR buf[NW];
    WCHAR cwd[MAX_PATH];
    WCHAR want[MAX_PATH + 16];
    static const WCHAR omega[] = { 'C', ':', '\\', 0x03A9, '.', 't', 'x', 't', 0 };   /* U+03A9 is not in code page 1252 */
    static const WCHAR lone[] = { 'C', ':', '\\', 0xD800, 'x', 0 };                   /* an unpaired surrogate */
    WCHAR *part;
    DWORD cn, n, wl;

    T_CASE("a relative name gives the current directory, a backslash and the name, and the part points at the name");
    cn = rt_path_get_cwd_w(cwd, MAX_PATH);
    T_REQUIRE(cn + 8 < MAX_PATH);
    copy_w(want, cwd, cn);
    put(want + cn, "\\x.txt");
    wl = cn + 6;
    fill_w(buf, 'Z', NW);
    part = NULL;
    n = CALL(kernel32, GetFullPathNameW, W("x.txt"), NW, buf, &part);
    T_EQ(n, wl);
    T_TRUE(same_w(buf, want, wl + 1));
    T_TRUE(all_w(buf + wl + 1, 'Z', NW - wl - 1));
    T_TRUE(part == buf + cn + 1);

    T_CASE("a name with dot-dot is normalised and the part points at its last component");
    fill_w(buf, 'Z', NW);
    part = NULL;
    n = CALL(kernel32, GetFullPathNameW, W("C:\\a\\..\\b\\c.bin"), NW, buf, &part);
    T_EQ(n, 10);
    T_TRUE(is_w(buf, "C:\\b\\c.bin"));
    T_TRUE(all_w(buf + 11, 'Z', NW - 11));
    T_TRUE(part == buf + 5);

    T_CASE("a name that ends in a separator gives a NULL part");
    fill_w(buf, 'Z', NW);
    part = buf + 3;
    n = CALL(kernel32, GetFullPathNameW, W("C:\\a\\"), NW, buf, &part);
    T_EQ(n, 5);
    T_TRUE(is_w(buf, "C:\\a\\"));
    T_TRUE(all_w(buf + 6, 'Z', NW - 6));
    T_TRUE(part == NULL);

    T_CASE("a character outside code page 1252 comes back as it is");
    fill_w(buf, 'Z', NW);
    part = NULL;
    n = CALL(kernel32, GetFullPathNameW, omega, NW, buf, &part);
    T_EQ(n, 8);
    T_TRUE(same_w(buf, omega, 9));
    T_TRUE(all_w(buf + 9, 'Z', NW - 9));
    T_TRUE(part == buf + 3);

    T_CASE("lpFilePart NULL is accepted: the length is returned and the buffer is written");
    fill_w(buf, 'Z', NW);
    n = CALL(kernel32, GetFullPathNameW, W("C:\\a\\..\\b\\c.bin"), NW, buf, NULL);
    T_EQ(n, 10);
    T_TRUE(is_w(buf, "C:\\b\\c.bin"));
    T_TRUE(all_w(buf + 11, 'Z', NW - 11));

    T_CASE("the size query with a zero buffer returns the length plus one and writes nothing");
    fill_w(buf, 'Z', NW);
    part = buf + 3;
    n = CALL(kernel32, GetFullPathNameW, W("x.txt"), 0, NULL, NULL);
    T_EQ(n, wl + 1);
    n = CALL(kernel32, GetFullPathNameW, W("x.txt"), 0, NULL, &part);
    T_EQ(n, wl + 1);
    T_TRUE(part == buf + 3);
    n = CALL(kernel32, GetFullPathNameW, W("x.txt"), 0, buf, &part);
    T_EQ(n, wl + 1);
    T_TRUE(all_w(buf, 'Z', NW));
    T_TRUE(part == buf + 3);

    T_CASE("a buffer one WCHAR too small returns the length plus one and writes neither the buffer nor the part");
    fill_w(buf, 'Z', NW);
    part = buf + 3;
    n = CALL(kernel32, GetFullPathNameW, W("x.txt"), wl, buf, &part);
    T_EQ(n, wl + 1);
    T_TRUE(all_w(buf, 'Z', NW));
    T_TRUE(part == buf + 3);

    T_CASE("NULL, an empty name and a name of spaces only fail with ERROR_INVALID_NAME and write nothing");
    fill_w(buf, 'Z', NW);
    part = buf + 3;
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, GetFullPathNameW, NULL, NW, buf, &part);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_NAME);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, GetFullPathNameW, W(""), NW, buf, &part);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_NAME);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, GetFullPathNameW, W("   "), NW, buf, &part);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_NAME);
    T_TRUE(all_w(buf, 'Z', NW));
    T_TRUE(part == buf + 3);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, GetFullPathNameW, W("C:\\a\\b"), NW, buf, &part);
    T_EQ(n, 6);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, GetFullPathNameW, W("x.txt"), NW, buf, NULL);
    T_EQ(n, wl);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stops.  Each proves an RT-TRAPS entry; they come after the checks of the last case. */
    T_TRAPS(CALL(kernel32, GetFullPathNameW, W("x.txt"), NW, NULL, NULL), "lpBuffer NULL with a nonzero nBufferLength");
    T_TRAPS(CALL(kernel32, GetFullPathNameW, W("\\\\."), NW, buf, &part), "with nothing after it");
    T_TRAPS(CALL(kernel32, GetFullPathNameW, W("\\\\?"), NW, buf, &part), "with nothing after it");
    T_TRAPS(CALL(kernel32, GetFullPathNameW, W("\\\\"), NW, buf, &part), "a UNC path without a server name");
    T_TRAPS(CALL(kernel32, GetFullPathNameW, W("\\\\\\x"), NW, buf, &part), "a UNC path without a server name");
    T_TRAPS(CALL(kernel32, GetFullPathNameW, W("C:\\dir\\nul.txt"), NW, buf, &part), "a DOS device name followed by '.' or ':'");
    T_TRAPS(CALL(kernel32, GetFullPathNameW, W("C:\\dir\\NUL:"), NW, buf, &part), "a DOS device name followed by '.' or ':'");
    T_TRAPS(CALL(kernel32, GetFullPathNameW, lone, NW, buf, &part), "unpaired UTF-16 surrogate");
}
