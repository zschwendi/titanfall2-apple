#include "rt/test.h"
#include "rt/locale.h"

/* Every byte the call does not write is left at SENT, so a write past the end shows.  The
 * oracle is rt_locale_info: its WCHARs, one byte each (en-US is ASCII). */
#define BUF  16
#define SENT ((char)0xCC)

static void fill(char *b, int n) {
    for (int i = 0; i < n; i++)
        b[i] = SENT;
}

/* TRUE if b holds `want` followed by its NUL. */
static int is_text(const char *b, const char *want) {
    int i = 0;
    for (; want[i]; i++)
        if (b[i] != want[i])
            return 0;
    return b[i] == 0;
}

/* TRUE if b[from..to) still holds the sentinel. */
static int untouched(const char *b, int from, int to) {
    for (int i = from; i < to; i++)
        if (b[i] != SENT)
            return 0;
    return 1;
}

/* The core's en-US text for `type`, one byte per WCHAR, NUL included: returns its length. */
static int core_text(LCTYPE type, char *want) {
    WCHAR w[256];
    int m = rt_locale_info(0x0409, type, w, 256);
    if (m <= 0)
        return 0;
    for (int i = 0; i < m; i++)
        want[i] = (char)w[i];
    return m;
}

/* TRUE if b[0..n) equals want[0..n). */
static int same(const char *b, const char *want, int n) {
    for (int i = 0; i < n; i++)
        if (b[i] != want[i])
            return 0;
    return 1;
}

/* The DWORD a LOCALE_RETURN_NUMBER call stores at the start of the buffer (little-endian). */
static DWORD dword_at(const char *b) {
    const unsigned char *p = (const unsigned char *)b;
    return (DWORD)p[0] | (DWORD)p[1] << 8 | (DWORD)p[2] << 16 | (DWORD)p[3] << 24;
}

TEST(kernel32, GetLocaleInfoA)
{
    char buf[BUF];
    char want[256];
    int n;

    T_CASE("LOCALE_SNAME as text: en-US and its NUL");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME, buf, BUF), 6);
    T_TRUE(is_text(buf, "en-US"));
    T_TRUE(untouched(buf, 6, BUF));
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME, buf, 6), 6);    /* exactly the size fits */
    T_TRUE(is_text(buf, "en-US"));
    T_TRUE(untouched(buf, 6, BUF));

    T_CASE("LOCALE_SENGLANGUAGE and LOCALE_SDECIMAL as text: the core's text, same length and characters");
    fill(buf, BUF);
    n = core_text(LOCALE_SENGLANGUAGE, want);
    T_TRUE(n > 0);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SENGLANGUAGE, buf, BUF), n);
    T_TRUE(same(buf, want, n));
    T_TRUE(untouched(buf, n, BUF));
    fill(buf, BUF);
    n = core_text(LOCALE_SDECIMAL, want);
    T_TRUE(n > 0);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SDECIMAL, buf, BUF), n);
    T_TRUE(is_text(buf, "."));
    T_TRUE(same(buf, want, n));
    T_TRUE(untouched(buf, n, BUF));

    T_CASE("the size query returns the length with the NUL and writes nothing");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME, buf, 0), 6);
    T_TRUE(untouched(buf, 0, BUF));
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME, NULL, 0), 6);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_IDEFAULTANSICODEPAGE, NULL, 0), 5);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SDECIMAL, buf, 0), 2);
    T_TRUE(untouched(buf, 0, BUF));

    T_CASE("a buffer one byte too small fails with ERROR_INSUFFICIENT_BUFFER and is filled up to its size");
    CALL(kernel32, SetLastError, 0);
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME, buf, 5), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(same(buf, "en-US", 5));
    T_TRUE(untouched(buf, 5, BUF));
    CALL(kernel32, SetLastError, 0);
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SDECIMAL, buf, 1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(same(buf, ".", 1));
    T_TRUE(untouched(buf, 1, BUF));
    CALL(kernel32, SetLastError, 0);
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME, buf, 3), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(same(buf, "en-", 3));
    T_TRUE(untouched(buf, 3, BUF));

    T_CASE("LOCALE_IDEFAULTANSICODEPAGE with LOCALE_RETURN_NUMBER: 1252 stored in 4 bytes");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, BUF), 4);
    T_EQ(dword_at(buf), 1252);
    T_TRUE(untouched(buf, 4, BUF));

    T_CASE("the size query with LOCALE_RETURN_NUMBER returns 4 and writes nothing");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, 0), 4);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, NULL, 0), 4);
    T_TRUE(untouched(buf, 0, BUF));

    T_CASE("LOCALE_RETURN_NUMBER into a buffer under 4 bytes fails with ERROR_INSUFFICIENT_BUFFER and writes nothing");
    CALL(kernel32, SetLastError, 0);
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, 3), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(untouched(buf, 0, BUF));
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, 1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(untouched(buf, 0, BUF));

    T_CASE("a negative length fails with ERROR_INVALID_PARAMETER and writes nothing");
    CALL(kernel32, SetLastError, 0);
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME, buf, -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_TRUE(untouched(buf, 0, BUF));
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, -4), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_TRUE(untouched(buf, 0, BUF));

    T_CASE("a NULL buffer with a nonzero length fails with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME, NULL, BUF), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, NULL, 4), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("LOCALE_NOUSEROVERRIDE and LOCALE_USE_CP_ACP change nothing");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME | LOCALE_NOUSEROVERRIDE, buf, BUF), 6);
    T_TRUE(is_text(buf, "en-US"));
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SDECIMAL | LOCALE_USE_CP_ACP, buf, BUF), 2);
    T_TRUE(is_text(buf, "."));
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409,
              LOCALE_IDEFAULTANSICODEPAGE | LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP | LOCALE_RETURN_NUMBER, buf, BUF), 4);
    T_EQ(dword_at(buf), 1252);
    T_TRUE(untouched(buf, 4, BUF));

    T_CASE("locale 0, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT are en-US");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0, LOCALE_SNAME, buf, BUF), 6);
    T_TRUE(is_text(buf, "en-US"));
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, LOCALE_USER_DEFAULT, LOCALE_SNAME, buf, BUF), 6);
    T_TRUE(is_text(buf, "en-US"));
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoA, LOCALE_SYSTEM_DEFAULT, LOCALE_SNAME, buf, BUF), 6);
    T_TRUE(is_text(buf, "en-US"));

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME, buf, BUF), 6);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, BUF), 4);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME, NULL, 0), 6);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: each core stop must end in a fatal report that names it. */
    T_TRAPS(CALL(kernel32, GetLocaleInfoA, 0x0407, LOCALE_SNAME, buf, BUF), "locale 0x407");
    T_TRAPS(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME | LOCALE_RETURN_GENITIVE_NAMES, buf, BUF),
            "LOCALE_RETURN_GENITIVE_NAMES");
    T_TRAPS(CALL(kernel32, GetLocaleInfoA, 0x0409, LOCALE_SNAME | LOCALE_RETURN_NUMBER, buf, 4),
            "LOCALE_RETURN_NUMBER");
    T_TRAPS(CALL(kernel32, GetLocaleInfoA, 0x0409, 0x0BAD, buf, BUF), "is not in the en-US table");
}
