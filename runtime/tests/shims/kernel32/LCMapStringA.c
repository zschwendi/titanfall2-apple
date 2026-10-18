#include "rt/test.h"
#include "rt/win/error.h"
#include "rt/win/nls.h"

/* Copies n bytes of s to d. */
static void put(char *d, const char *s, int n)
{
    for (int i = 0; i < n; i++) d[i] = s[i];
}

/* Fills n bytes of d with c. */
static void rep(char *d, char c, int n)
{
    for (int i = 0; i < n; i++) d[i] = c;
}

/* Fills n bytes of d with the sentinel 0xEE, so that a write outside the expected range shows. */
static void fill(char *d, int n)
{
    rep(d, (char)0xEE, n);
}

/* Checks that the n bytes at p are the bytes of s. */
static void expect_bytes(const char *p, const char *s, int n)
{
    for (int i = 0; i < n; i++) T_EQ((unsigned char)p[i], (unsigned char)s[i]);
}

/* Checks that the n bytes at p are still the sentinel. */
static void expect_untouched(const char *p, int n)
{
    for (int i = 0; i < n; i++) T_EQ((unsigned char)p[i], 0xEE);
}

TEST(kernel32, LCMapStringA)
{
    static const unsigned locales[] = { 0x409, LOCALE_USER_DEFAULT, 0, LOCALE_SYSTEM_DEFAULT };
    char src[32], dst[32], big[1100];
    int n;

    T_CASE("upper and lower case in en-US");
    for (unsigned i = 0; i < sizeof locales / sizeof locales[0]; i++) {
        put(src, "Hello", 5);
        fill(dst, 8);
        n = CALL(kernel32, LCMapStringA, locales[i], LCMAP_UPPERCASE, src, 5, dst, 8);
        T_EQ(n, 5);
        expect_bytes(dst, "HELLO", 5);
        expect_untouched(dst + 5, 3);           /* no terminator when the length is explicit */

        put(src, "Hello\0", 6);
        fill(dst, 8);
        n = CALL(kernel32, LCMapStringA, locales[i], LCMAP_LOWERCASE, src, -1, dst, 8);
        T_EQ(n, 6);                             /* the NUL is mapped and counted */
        expect_bytes(dst, "hello\0", 6);
        expect_untouched(dst + 6, 2);

        src[0] = (char)0xE9;                    /* e acute in code page 1252 */
        fill(dst, 8);
        n = CALL(kernel32, LCMapStringA, locales[i], LCMAP_UPPERCASE, src, 1, dst, 8);
        T_EQ(n, 1);
        T_EQ((unsigned char)dst[0], 0xC9);
    }

    T_CASE("size query");
    put(src, "Hello, World", 12);
    fill(dst, 16);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 12, dst, 0);
    T_EQ(n, 12);
    expect_untouched(dst, 16);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 12, NULL, 0);
    T_EQ(n, 12);
    put(src, "ab\0", 3);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, -1, NULL, 0);
    T_EQ(n, 3);                                 /* the NUL is counted */

    T_CASE("buffer one byte too small");
    put(src, "abc", 3);
    fill(dst, 8);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 3, dst, 2);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    expect_untouched(dst, 8);                   /* the A form writes nothing */

    T_CASE("in-place mapping");
    put(src, "abc", 3);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 3, src, 3);
    T_EQ(n, 3);
    expect_bytes(src, "ABC", 3);

    put(src, "xY\0", 3);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_LOWERCASE, src, -1, src, 3);
    T_EQ(n, 3);                                 /* the NUL is mapped and counted */
    expect_bytes(src, "xy\0", 3);

    T_CASE("linguistic casing with a case flag");
    put(src, "ABC", 3);
    fill(dst, 8);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING, src, 3, dst, 3);
    T_EQ(n, 3);
    expect_bytes(dst, "abc", 3);
    expect_untouched(dst + 3, 5);

    T_CASE("LOCALE_USE_CP_ACP changes nothing");
    put(src, "AB\0", 3);
    fill(dst, 8);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, LCMapStringA, LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP | LCMAP_LOWERCASE, src, -1, dst, 8);
    T_EQ(n, 3);                                 /* the NUL is mapped and counted */
    expect_bytes(dst, "ab\0", 3);
    expect_untouched(dst + 3, 5);
    T_EQ(T_LAST_ERROR(), 0x1234);
    n = CALL(kernel32, LCMapStringA, LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP | LCMAP_LOWERCASE, src, -1, NULL, 0);
    T_EQ(n, 3);

    T_CASE("parameter failures");
    put(src, "abc", 3);
    fill(dst, 8);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, NULL, 3, dst, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 0, dst, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 3, dst, -1);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    expect_untouched(dst, 8);

    /* No buffer with a size that is not 0: a failure, as rt_lcmap_string gives for the W form
     * (rt/locale.h; docs/reviews/shim-wave3-review.md, follow-up 4), not a stop. */
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 3, NULL, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    CALL(kernel32, SetLastError, 0);            /* the parameter check comes before the flags check */
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_SORTKEY, NULL, 0, NULL, 0);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, 0, src, 0, dst, 8);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    expect_untouched(dst, 8);

    T_CASE("flag failures");
    put(src, "abc", 3);
    fill(dst, 8);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_LINGUISTIC_CASING, src, 3, dst, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 8);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, 0, src, 3, dst, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 8);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE | 0x40, src, 3, dst, 3);
    T_EQ(n, 0);                                 /* 0x40 is none of the known flag values */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 8);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_LOWERCASE | SORT_STRINGSORT, src, 3, dst, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 8);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE | NORM_IGNORESYMBOLS, src, 3, dst, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 8);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE | NORM_IGNORENONSPACE, src, 3, dst, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 8);

    T_CASE("success leaves last error alone");
    put(src, "abc", 3);
    fill(dst, 8);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 3, dst, 3);
    T_EQ(n, 3);
    T_EQ(T_LAST_ERROR(), 0x1234);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 3, NULL, 0);
    T_EQ(n, 3);
    T_EQ(T_LAST_ERROR(), 0x1234);
    n = CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 3, src, 3);
    T_EQ(n, 3);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stops, each a trap: the shim's own refusals first, then the core's fatal cases.  The
     * core's cases use valid parameters (a NUL-terminated source, length -1, a size query). */
    rep(big, 'a', 1100);
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, big, 1100, NULL, 0), "LCMapStringA with more than 1024 characters");
    src[0] = (char)0xB5;                        /* micro sign: its upper case U+039C is not in code page 1252 */
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE, src, 1, dst, 8), "LCMapStringA result character outside code page 1252");
    put(src, "abc\0", 4);
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x409, LCMAP_TITLECASE, src, -1, NULL, 0), "LCMAP_TITLECASE");
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE | LCMAP_SORTKEY, src, -1, NULL, 0), "combine a case mapping with 0x400,");
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x409, LCMAP_LOWERCASE | LCMAP_BYTEREV, src, -1, NULL, 0), "combine a case mapping with 0x800,");
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE | LCMAP_HASH, src, -1, NULL, 0), "combine a case mapping with 0x40000,");
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE | LCMAP_HIRAGANA, src, -1, NULL, 0), "combine a case mapping with 0x100000,");
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE | NORM_IGNORECASE, src, -1, NULL, 0), "combine a case mapping with 0x1,");
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x409, LCMAP_UPPERCASE | LCMAP_SORTHANDLE, src, -1, NULL, 0), "combine a case mapping with 0x20000000,");
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x409, SORT_STRINGSORT, src, -1, NULL, 0), "flags 0x1000 without LCMAP_LOWERCASE or LCMAP_UPPERCASE");
    T_TRAPS(CALL(kernel32, LCMapStringA, 0x407, LCMAP_UPPERCASE, src, 3, dst, 3), "locale 0x407");
}
