#include "rt/test.h"
#include "rt/win/error.h"
#include "rt/win/nls.h"

#define SENTINEL ((WCHAR)0xFFFF)

/* Copies n ASCII characters of s to d as WCHARs. */
static void wide(WCHAR *d, const char *s, int n)
{
    for (int i = 0; i < n; i++) d[i] = (WCHAR)(unsigned char)s[i];
}

/* Fills n WCHARs of d with the sentinel, so that a write outside the expected range shows. */
static void fill(WCHAR *d, int n)
{
    for (int i = 0; i < n; i++) d[i] = SENTINEL;
}

/* Checks that the n WCHARs at p are the ASCII characters of s. */
static void expect_text(const WCHAR *p, const char *s, int n)
{
    for (int i = 0; i < n; i++) T_EQ(p[i], (WCHAR)(unsigned char)s[i]);
}

/* Checks that n WCHARs at p are still the sentinel. */
static void expect_untouched(const WCHAR *p, int n)
{
    for (int i = 0; i < n; i++) T_EQ(p[i], SENTINEL);
}

TEST(kernel32, LCMapStringW)
{
    static const unsigned locales[] = { 0x409, LOCALE_USER_DEFAULT, 0, LOCALE_SYSTEM_DEFAULT };
    WCHAR src[32], dst[32], big[32];
    int n;

    T_CASE("upper and lower case in en-US");
    for (unsigned i = 0; i < sizeof locales / sizeof locales[0]; i++) {
        wide(src, "Hello, World", 12);
        fill(dst, 16);
        n = CALL(kernel32, LCMapStringW, locales[i], LCMAP_UPPERCASE, src, 12, dst, 12);
        T_EQ(n, 12);
        expect_text(dst, "HELLO, WORLD", 12);
        expect_untouched(dst + 12, 4);

        wide(src, "Hello, World", 12);
        fill(dst, 16);
        n = CALL(kernel32, LCMapStringW, locales[i], LCMAP_LOWERCASE, src, 12, dst, 12);
        T_EQ(n, 12);
        expect_text(dst, "hello, world", 12);
        expect_untouched(dst + 12, 4);
    }
    /* Latin-1 letters are in the case table: U+00C9 (E acute) lowers to U+00E9. */
    src[0] = 0x00C9; src[1] = (WCHAR)'C'; src[2] = (WCHAR)'O';
    fill(dst, 16);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_LOWERCASE, src, 3, dst, 3);
    T_EQ(n, 3);
    T_EQ(dst[0], (WCHAR)0x00E9);
    T_EQ(dst[1], (WCHAR)'c');
    T_EQ(dst[2], (WCHAR)'o');
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("size query");
    wide(src, "Hello, World", 12);
    fill(dst, 16);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, src, 12, dst, 0);
    T_EQ(n, 12);
    expect_untouched(dst, 16);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, src, 12, NULL, 0);
    T_EQ(n, 12);
    wide(src, "ab", 2);
    src[2] = 0;
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, src, -1, NULL, 0);
    T_EQ(n, 3);                             /* the NUL is counted */

    T_CASE("buffer one too small");
    wide(src, "abc", 3);
    fill(dst, 16);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, src, 3, dst, 2);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    expect_text(dst, "AB", 2);              /* the part that fits is written */
    expect_untouched(dst + 2, 4);

    T_CASE("source length -1 and explicit length");
    wide(src, "ab", 2);
    src[2] = 0;
    fill(dst, 16);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, src, -1, dst, 3);
    T_EQ(n, 3);                             /* "AB" and the NUL */
    expect_text(dst, "AB", 2);
    T_EQ(dst[2], 0);
    expect_untouched(dst + 3, 4);

    fill(dst, 16);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, src, -5, dst, 3);
    T_EQ(n, 3);                             /* any negative length is NUL-terminated */
    expect_text(dst, "AB", 2);
    T_EQ(dst[2], 0);

    wide(src, "abc", 3);
    fill(dst, 16);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, src, 2, dst, 2);
    T_EQ(n, 2);                             /* explicit length: no NUL is added */
    expect_text(dst, "AB", 2);
    expect_untouched(dst + 2, 4);

    T_CASE("in-place mapping");
    wide(big, "abc", 3);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, big, 3, big, 3);
    T_EQ(n, 3);
    expect_text(big, "ABC", 3);
    T_EQ(T_LAST_ERROR(), 0x1234);

    wide(big, "xY", 2);
    big[2] = 0;
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_LOWERCASE, big, -1, big, 3);
    T_EQ(n, 3);                             /* the NUL is mapped and counted */
    expect_text(big, "xy", 2);
    T_EQ(big[2], 0);

    T_CASE("linguistic casing with a case flag");
    wide(src, "ABC", 3);
    fill(dst, 16);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING, src, 3, dst, 3);
    T_EQ(n, 3);
    expect_text(dst, "abc", 3);
    expect_untouched(dst + 3, 4);

    wide(src, "abc", 3);
    fill(dst, 16);
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, LCMapStringW, LOCALE_USER_DEFAULT, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, src, 3, dst, 3);
    T_EQ(n, 3);
    expect_text(dst, "ABC", 3);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("parameter failures");
    wide(src, "abc", 3);
    fill(dst, 16);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, NULL, 3, dst, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, src, 0, dst, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, src, 3, dst, -1);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE, src, 3, NULL, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    expect_untouched(dst, 16);

    CALL(kernel32, SetLastError, 0);        /* the parameter check comes before the flags check */
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_SORTKEY, NULL, 0, NULL, 0);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    wide(src, "abc", 3);
    fill(dst, 8);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, 0, src, 0, dst, 8);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    expect_untouched(dst, 8);

    T_CASE("flag failures");
    wide(src, "abc", 3);
    fill(dst, 16);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_LINGUISTIC_CASING, src, 3, dst, 3);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 16);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE | 0x40, src, 3, dst, 3);
    T_EQ(n, 0);                             /* 0x40 is none of the known flag values */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 16);

    wide(src, "abc", 3);                    /* NUL-terminated for the length -1 calls below */
    src[3] = 0;
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_SORTKEY | 0x40, src, -1, NULL, 0);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_LINGUISTIC_CASING | NORM_IGNORECASE, src, -1, NULL, 0);
    T_EQ(n, 0);                             /* LCMAP_LINGUISTIC_CASING needs a case flag */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);

    /* Windows fails flags 0, and a case flag with SORT_STRINGSORT, NORM_IGNORESYMBOLS or
     * NORM_IGNORENONSPACE, with ERROR_INVALID_FLAGS and no output (Wine kernel32/tests/locale.c,
     * the array lcmap_invalid_flags used by test_lcmapstring_unicode for LCMapStringW). */
    fill(dst, 8);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, 0, src, -1, dst, 8);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 8);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_LOWERCASE | NORM_IGNORESYMBOLS, src, -1, dst, 8);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 8);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE | NORM_IGNORENONSPACE, src, -1, dst, 8);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 8);

    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, LCMapStringW, 0x409, LCMAP_LOWERCASE | SORT_STRINGSORT, src, -1, dst, 8);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    expect_untouched(dst, 8);

    /* Refused flags: each group is a fatal report from the core, so each is a trap.  Valid
     * parameters (a NUL-terminated source, length -1, a size query) reach the flags check. */
    T_TRAPS(CALL(kernel32, LCMapStringW, 0x409, LCMAP_TITLECASE, src, -1, NULL, 0), "LCMAP_TITLECASE");
    T_TRAPS(CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE | LCMAP_SORTKEY, src, -1, NULL, 0), "combine a case mapping with 0x400,");
    T_TRAPS(CALL(kernel32, LCMapStringW, 0x409, LCMAP_LOWERCASE | LCMAP_BYTEREV, src, -1, NULL, 0), "combine a case mapping with 0x800,");
    T_TRAPS(CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE | LCMAP_HASH, src, -1, NULL, 0), "combine a case mapping with 0x40000,");
    T_TRAPS(CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE | LCMAP_HIRAGANA, src, -1, NULL, 0), "combine a case mapping with 0x100000,");
    T_TRAPS(CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE | NORM_IGNORECASE, src, -1, NULL, 0), "combine a case mapping with 0x1,");
    T_TRAPS(CALL(kernel32, LCMapStringW, 0x409, LCMAP_UPPERCASE | LCMAP_SORTHANDLE, src, -1, NULL, 0), "combine a case mapping with 0x20000000,");
}
