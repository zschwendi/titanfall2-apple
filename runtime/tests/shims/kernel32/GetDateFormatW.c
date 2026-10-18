#include "rt/test.h"
#include "rt/win/nls.h"

/* A NUL-terminated UTF-16 string from a C string: one WCHAR per byte of `s`.  WCHAR is
 * uint16_t; wchar_t is never used. */
static void wide(WCHAR *out, const char *s) {
    for (; *s; s++) *out++ = (unsigned char)*s;
    *out = 0;
}

/* True if the NUL-terminated UTF-16 string `w` is the C string `s`. */
static int wide_is(const WCHAR *w, const char *s) {
    for (; *s; s++, w++) if (*w != (unsigned char)*s) return 0;
    return *w == 0;
}

/* Bytes the function must not touch hold this sentinel, which no expected text contains. */
enum { SENTINEL = 0xFFFF };
static void fill(WCHAR *b, unsigned n) {
    for (unsigned i = 0; i < n; i++) b[i] = SENTINEL;
}

/* True if b[from..to) still holds the sentinel. */
static int untouched(const WCHAR *b, unsigned from, unsigned to) {
    for (unsigned i = from; i < to; i++) if (b[i] != SENTINEL) return 0;
    return 1;
}

static int clen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* True if the 64 WCHARs at a and b are equal. */
static int same(const WCHAR *a, const WCHAR *b) {
    for (unsigned i = 0; i < 64; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* Runs GetDateFormatW with locale 0 and with 0x0409, with the same other arguments, and checks
 * that the two calls agree on the return value and on the whole buffer (both start all sentinel),
 * and that neither changes last error.  The 0x0409 result is left in out; its return value is
 * returned. */
static int neutral_same(const SYSTEMTIME *date, DWORD flags, const WCHAR *pic, WCHAR *out) {
    WCHAR a[64];
    int na, nb;
    fill(a, 64);
    fill(out, 64);
    CALL(kernel32, SetLastError, 0x1234);
    na = CALL(kernel32, GetDateFormatW, 0, flags, date, pic, a, 64);
    T_EQ(T_LAST_ERROR(), 0x1234);
    nb = CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, flags, date, pic, out, 64);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(na, nb);
    T_TRUE(same(a, out));
    return nb;
}

/* Friday 9 October 2026 (the day of the week is the right one); the time is not used. */
static const SYSTEMTIME fixed = { 2026, 10, 5, 9, 14, 5, 9, 250 };

TEST(kernel32, GetDateFormatW)
{
    WCHAR buf[64], pic[64];
    SYSTEMTIME d;
    int n;

    T_CASE("the short date with flags 0");
    fill(buf, 64);
    n = CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, NULL, buf, 64);
    T_EQ(n, 10);
    T_TRUE(wide_is(buf, "10/9/2026"));
    T_TRUE(untouched(buf, 10, 64));
    fill(buf, 64);                          /* a buffer of exactly the needed size is enough */
    n = CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, NULL, buf, 10);
    T_EQ(n, 10);
    T_TRUE(wide_is(buf, "10/9/2026"));
    T_TRUE(untouched(buf, 10, 64));

    T_CASE("the long date with DATE_LONGDATE and the year-month with DATE_YEARMONTH");
    fill(buf, 64);
    n = CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, DATE_LONGDATE, &fixed, NULL, buf, 64);
    T_EQ(n, clen("Friday, October 09, 2026") + 1);
    T_TRUE(wide_is(buf, "Friday, October 09, 2026"));
    T_TRUE(untouched(buf, n, 64));
    fill(buf, 64);
    n = CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, DATE_YEARMONTH, &fixed, NULL, buf, 64);
    T_EQ(n, clen("October, 2026") + 1);
    T_TRUE(wide_is(buf, "October, 2026"));
    T_TRUE(untouched(buf, n, 64));

    T_CASE("a format picture, with its letters and quoted text");
    wide(pic, "dddd, MMMM d, yyyy");
    fill(buf, 64);
    n = CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, pic, buf, 64);
    T_EQ(n, clen("Friday, October 9, 2026") + 1);
    T_TRUE(wide_is(buf, "Friday, October 9, 2026"));
    T_TRUE(untouched(buf, n, 64));
    wide(pic, "d dd ddd M MM MMM yy");
    fill(buf, 64);
    n = CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, pic, buf, 64);
    T_TRUE(wide_is(buf, "9 09 Fri 10 10 Oct 26"));
    T_TRUE(untouched(buf, n, 64));
    wide(pic, "dddddd MMMMM yyy yyyyy");    /* more than four d and M count as four; yyy and yyyyy as yyyy */
    fill(buf, 64);
    n = CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, pic, buf, 64);
    T_TRUE(wide_is(buf, "Friday October 2026 2026"));
    T_TRUE(untouched(buf, n, 64));
    wide(pic, "'It''s' 'y' 'g' d");           /* quoted text is copied; '' inside quotes is one quote */
    fill(buf, 64);
    n = CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, pic, buf, 64);
    T_TRUE(wide_is(buf, "It's y g 9"));
    T_TRUE(untouched(buf, n, 64));

    /* Wine's test_GetDateFormatW: an empty picture is a picture, not a stop. */
    T_CASE("an empty format picture gives the empty string with flags 0, and ERROR_INVALID_FLAGS with a DATE_ format flag");
    wide(pic, "");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, NULL, pic, NULL, 0), 1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    fill(buf, 64);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, NULL, pic, buf, 64), 1);
    T_EQ(buf[0], 0);
    T_TRUE(untouched(buf, 1, 64));
    T_EQ(T_LAST_ERROR(), 0x1234);
    fill(buf, 64);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LOCALE_SYSTEM_DEFAULT, DATE_LONGDATE, NULL, pic, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    T_TRUE(untouched(buf, 0, 64));

    T_CASE("the size query returns the length with the NUL and writes nothing");
    fill(buf, 64);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, NULL, NULL, 0), 10);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, NULL, buf, 0), 10);
    T_TRUE(untouched(buf, 0, 64));
    wide(pic, "yyyy-MM-dd");
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, pic, NULL, 0), 11);

    T_CASE("a buffer one too small fails with ERROR_INSUFFICIENT_BUFFER");
    fill(buf, 64);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, NULL, buf, 9), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(untouched(buf, 9, 64));          /* nothing is written at or past the length given */

    T_CASE("a NULL date formats the current date");
    fill(buf, 64);
    n = CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, NULL, NULL, buf, 64);
    T_REQUIRE(n > 0);
    T_TRUE(buf[n - 1] == 0);
    T_TRUE(untouched(buf, n, 64));

    T_CASE("an invalid date fails with ERROR_INVALID_PARAMETER");
    d = fixed; d.wMonth = 13;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &d, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    d = fixed; d.wDay = 0;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &d, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    d = fixed; d.wDay = 29; d.wMonth = 2;   /* 2026 is not a leap year */
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &d, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    d = fixed; d.wYear = 1600;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &d, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a bad flag fails with ERROR_INVALID_FLAGS");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0x100, &fixed, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, DATE_LONGDATE | DATE_SHORTDATE, &fixed, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    /* LOCALE_NOUSEROVERRIDE with a format picture: ERROR_INVALID_FLAGS, buffer untouched (Windows:
     * Wine test_GetDateFormatA, "Same as above but with LOCALE_NOUSEROVERRIDE"). */
    wide(pic, "yyyy");
    fill(buf, 64);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, LOCALE_NOUSEROVERRIDE, &fixed, pic, buf, 16), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    T_TRUE(untouched(buf, 0, 64));

    T_CASE("DATE_LTRREADING with DATE_RTLREADING fails with ERROR_INVALID_FLAGS");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, DATE_LTRREADING | DATE_RTLREADING, &fixed, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    wide(pic, "yyyy");
    CALL(kernel32, SetLastError, 0);        /* a DATE_ format flag with a format picture */
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, DATE_LONGDATE, &fixed, pic, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);

    T_CASE("a negative length fails with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, NULL, buf, -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("the default locales and the LOCALE_ flags are en-US");
    fill(buf, 64);
    T_EQ(CALL(kernel32, GetDateFormatW, LOCALE_USER_DEFAULT, 0, &fixed, NULL, buf, 64), 10);
    T_TRUE(wide_is(buf, "10/9/2026"));
    fill(buf, 64);
    T_EQ(CALL(kernel32, GetDateFormatW, LOCALE_SYSTEM_DEFAULT, 0, &fixed, NULL, buf, 64), 10);
    T_TRUE(wide_is(buf, "10/9/2026"));
    fill(buf, 64);
    T_EQ(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP, &fixed, NULL, buf, 64), 10);
    T_TRUE(wide_is(buf, "10/9/2026"));

    T_CASE("locale 0 gives the same result as 0x0409");
    T_EQ(neutral_same(&fixed, 0, NULL, buf), 10);
    T_TRUE(wide_is(buf, "10/9/2026"));
    T_EQ(neutral_same(&fixed, DATE_LONGDATE, NULL, buf), clen("Friday, October 09, 2026") + 1);
    T_TRUE(wide_is(buf, "Friday, October 09, 2026"));
    T_EQ(neutral_same(&fixed, DATE_YEARMONTH, NULL, buf), clen("October, 2026") + 1);
    T_TRUE(wide_is(buf, "October, 2026"));
    wide(pic, "dddd, MMMM d, yyyy");
    T_EQ(neutral_same(&fixed, 0, pic, buf), clen("Friday, October 9, 2026") + 1);
    T_TRUE(wide_is(buf, "Friday, October 9, 2026"));
    T_EQ(CALL(kernel32, GetDateFormatW, 0, 0, &fixed, NULL, NULL, 0), 10);
    d = fixed; d.wMonth = 13;
    fill(buf, 64);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetDateFormatW, 0, 0, &d, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_TRUE(untouched(buf, 0, 64));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, NULL, buf, 64);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The RT-TRAPS cases: each is a fatal report from RT_UNIMPLEMENTED, with the offending value. */
    T_TRAPS(CALL(kernel32, GetDateFormatW, 0x0407, 0, &fixed, NULL, buf, 64), "a locale other than en-US");
    T_TRAPS(CALL(kernel32, GetDateFormatW, LOCALE_INVARIANT, 0, &fixed, NULL, buf, 64), "a locale other than en-US");
    T_TRAPS(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, DATE_USE_ALT_CALENDAR, &fixed, NULL, buf, 64), "DATE_USE_ALT_CALENDAR");
    T_TRAPS(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, DATE_LTRREADING, &fixed, NULL, buf, 64), "DATE_LTRREADING");
    T_TRAPS(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, DATE_RTLREADING, &fixed, NULL, buf, 64), "DATE_RTLREADING");
    T_TRAPS(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, DATE_AUTOLAYOUT, &fixed, NULL, buf, 64), "DATE_AUTOLAYOUT");
    wide(pic, "d y");
    T_TRAPS(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, pic, buf, 64), "a single unquoted y");
    wide(pic, "g");
    T_TRAPS(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, pic, buf, 64), "an unquoted g");
    wide(pic, "gg");
    T_TRAPS(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, pic, buf, 64), "an unquoted g");
    T_TRAPS(CALL(kernel32, GetDateFormatW, LANG_ENGLISH_US_LCID, 0, &fixed, NULL, NULL, 8), "a NULL buffer with a nonzero length");
}
