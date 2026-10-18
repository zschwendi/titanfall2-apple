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

/* Runs GetTimeFormatW with locale 0 and with 0x0409, with the same other arguments, and checks
 * that the two calls agree on the return value and on the whole buffer (both start all sentinel),
 * and that neither changes last error.  The 0x0409 result is left in out; its return value is
 * returned. */
static int neutral_same(const SYSTEMTIME *when, DWORD flags, const WCHAR *pic, WCHAR *out) {
    WCHAR a[64];
    int na, nb;
    fill(a, 64);
    fill(out, 64);
    CALL(kernel32, SetLastError, 0x1234);
    na = CALL(kernel32, GetTimeFormatW, 0, flags, when, pic, a, 64);
    T_EQ(T_LAST_ERROR(), 0x1234);
    nb = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, flags, when, pic, out, 64);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(na, nb);
    T_TRUE(same(a, out));
    return nb;
}

/* 14:05:09.250 (the date is not used), and 09:05:09.250 for the 24-hour hour with no leading zero. */
static const SYSTEMTIME at = { 2026, 10, 5, 9, 14, 5, 9, 250 };
static const SYSTEMTIME am = { 2026, 10, 5, 9, 9, 5, 9, 250 };

TEST(kernel32, GetTimeFormatW)
{
    WCHAR buf[64], pic[64];
    SYSTEMTIME t;
    int n;

    T_CASE("the locale format h:mm:ss tt with flags 0");
    fill(buf, 64);
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, NULL, buf, 64);
    T_EQ(n, clen("2:05:09 PM") + 1);
    T_TRUE(wide_is(buf, "2:05:09 PM"));
    T_TRUE(untouched(buf, n, 64));
    fill(buf, 64);                          /* a buffer of exactly the needed size is enough */
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, NULL, buf, 11);
    T_EQ(n, 11);
    T_TRUE(wide_is(buf, "2:05:09 PM"));
    T_TRUE(untouched(buf, 11, 64));

    T_CASE("TIME_NOSECONDS, TIME_NOTIMEMARKER, TIME_NOMINUTESORSECONDS and TIME_FORCE24HOURFORMAT");
    fill(buf, 64);
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, TIME_NOSECONDS, &at, NULL, buf, 64);
    T_EQ(n, clen("2:05 PM") + 1);
    T_TRUE(wide_is(buf, "2:05 PM"));
    T_TRUE(untouched(buf, n, 64));
    fill(buf, 64);
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, TIME_NOTIMEMARKER, &at, NULL, buf, 64);
    T_EQ(n, clen("2:05:09") + 1);
    T_TRUE(wide_is(buf, "2:05:09"));
    T_TRUE(untouched(buf, n, 64));
    fill(buf, 64);
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, TIME_NOMINUTESORSECONDS, &at, NULL, buf, 64);
    T_EQ(n, clen("2 PM") + 1);
    T_TRUE(wide_is(buf, "2 PM"));
    T_TRUE(untouched(buf, n, 64));
    fill(buf, 64);                          /* the 24-hour hour has no leading zero; the marker stays */
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, TIME_FORCE24HOURFORMAT, &at, NULL, buf, 64);
    T_EQ(n, clen("14:05:09 PM") + 1);
    T_TRUE(wide_is(buf, "14:05:09 PM"));
    T_TRUE(untouched(buf, n, 64));
    fill(buf, 64);
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, TIME_FORCE24HOURFORMAT | TIME_NOTIMEMARKER, &at, NULL, buf, 64);
    T_EQ(n, clen("14:05:09") + 1);
    T_TRUE(wide_is(buf, "14:05:09"));
    T_TRUE(untouched(buf, n, 64));
    fill(buf, 64);
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, TIME_FORCE24HOURFORMAT, &am, NULL, buf, 64);
    T_TRUE(wide_is(buf, "9:05:09 AM"));
    T_TRUE(untouched(buf, n, 64));

    T_CASE("a format picture, with its letters and quoted text");
    wide(pic, "h hh H HH m mm s ss t tt");
    fill(buf, 64);
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, pic, buf, 64);
    T_TRUE(wide_is(buf, "2 02 14 14 5 05 9 09 P PM"));
    T_TRUE(untouched(buf, n, 64));
    wide(pic, "'at' h 'o''clock'");         /* quoted text is copied; '' inside quotes is one quote */
    fill(buf, 64);
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, pic, buf, 64);
    T_TRUE(wide_is(buf, "at 2 o'clock"));
    T_TRUE(untouched(buf, n, 64));

    T_CASE("the size query returns the length with the NUL and writes nothing");
    fill(buf, 64);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, NULL, NULL, 0), 11);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, NULL, buf, 0), 11);
    T_TRUE(untouched(buf, 0, 64));
    wide(pic, "HH:mm");
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, pic, NULL, 0), 6);

    T_CASE("a buffer one too small fails with ERROR_INSUFFICIENT_BUFFER");
    fill(buf, 64);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, NULL, buf, 10), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(untouched(buf, 10, 64));         /* nothing is written at or past the length given */

    T_CASE("a NULL time formats the current time");
    fill(buf, 64);
    n = CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, NULL, NULL, buf, 64);
    T_REQUIRE(n > 0);
    T_TRUE(buf[n - 1] == 0);
    T_TRUE(untouched(buf, n, 64));

    T_CASE("an invalid time fails with ERROR_INVALID_PARAMETER");
    t = at; t.wHour = 24;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &t, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    t = at; t.wMinute = 60;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &t, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    t = at; t.wSecond = 60;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &t, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    t = at; t.wMilliseconds = 1000;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &t, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a bad flag fails with ERROR_INVALID_FLAGS");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0x100, &at, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    wide(pic, "HH");
    CALL(kernel32, SetLastError, 0);        /* a bad flag with a format picture is still a bad flag */
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0x100, &at, pic, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    /* LOCALE_NOUSEROVERRIDE with a format picture: ERROR_INVALID_FLAGS, buffer untouched (Windows:
     * MSDN, "LOCALE_NOUSEROVERRIDE can't be specified with a format string"; Wine test_GetTimeFormatA). */
    fill(buf, 64);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, LOCALE_NOUSEROVERRIDE, &at, pic, buf, 16), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    T_TRUE(untouched(buf, 0, 64));

    T_CASE("a negative length fails with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, NULL, buf, -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("the default locales and the LOCALE_ flags are en-US");
    fill(buf, 64);
    T_EQ(CALL(kernel32, GetTimeFormatW, LOCALE_USER_DEFAULT, 0, &at, NULL, buf, 64), 11);
    T_TRUE(wide_is(buf, "2:05:09 PM"));
    fill(buf, 64);
    T_EQ(CALL(kernel32, GetTimeFormatW, LOCALE_SYSTEM_DEFAULT, 0, &at, NULL, buf, 64), 11);
    T_TRUE(wide_is(buf, "2:05:09 PM"));
    fill(buf, 64);
    T_EQ(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP, &at, NULL, buf, 64), 11);
    T_TRUE(wide_is(buf, "2:05:09 PM"));

    T_CASE("locale 0 gives the same result as 0x0409");
    T_EQ(neutral_same(&at, 0, NULL, buf), 11);
    T_TRUE(wide_is(buf, "2:05:09 PM"));
    T_EQ(neutral_same(&at, TIME_NOSECONDS, NULL, buf), clen("2:05 PM") + 1);
    T_TRUE(wide_is(buf, "2:05 PM"));
    T_EQ(neutral_same(&at, TIME_FORCE24HOURFORMAT | TIME_NOTIMEMARKER, NULL, buf), clen("14:05:09") + 1);
    T_TRUE(wide_is(buf, "14:05:09"));
    wide(pic, "h hh H HH m mm s ss t tt");
    T_EQ(neutral_same(&at, 0, pic, buf), clen("2 02 14 14 5 05 9 09 P PM") + 1);
    T_TRUE(wide_is(buf, "2 02 14 14 5 05 9 09 P PM"));
    T_EQ(CALL(kernel32, GetTimeFormatW, 0, 0, &at, NULL, NULL, 0), 11);
    t = at; t.wHour = 24;
    fill(buf, 64);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetTimeFormatW, 0, 0, &t, NULL, buf, 64), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_TRUE(untouched(buf, 0, 64));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, NULL, buf, 64);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The RT-TRAPS cases: each is a fatal report from RT_UNIMPLEMENTED, with the offending value. */
    wide(pic, "");
    T_TRAPS(CALL(kernel32, GetTimeFormatW, 0x0407, 0, &at, NULL, buf, 64), "a locale other than en-US");
    T_TRAPS(CALL(kernel32, GetTimeFormatW, LOCALE_INVARIANT, 0, &at, NULL, buf, 64), "a locale other than en-US");
    T_TRAPS(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, pic, buf, 64), "an empty format picture");
    wide(pic, "HH:mm");
    T_TRAPS(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, TIME_NOSECONDS, &at, pic, buf, 64), "TIME_* flags with a format picture");
    T_TRAPS(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, TIME_FORCE24HOURFORMAT, &at, pic, buf, 64), "TIME_* flags with a format picture");
    T_TRAPS(CALL(kernel32, GetTimeFormatW, LANG_ENGLISH_US_LCID, 0, &at, NULL, NULL, 8), "a NULL buffer with a nonzero length");
}
