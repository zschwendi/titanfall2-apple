#include "rt/test.h"
#include "rt/win/nls.h"

/* Every unit the call does not write is left at SENT, so a write past the end shows.  The
 * expected text is ASCII; the guest buffer is WCHARs. */
#define BUF  16
#define SENT ((WCHAR)0xCCCC)

static void fill(WCHAR *b, int n) {
    for (int i = 0; i < n; i++)
        b[i] = SENT;
}

/* TRUE if b holds `want` followed by its NUL. */
static int is_text(const WCHAR *b, const char *want) {
    int i = 0;
    for (; want[i]; i++)
        if (b[i] != (WCHAR)want[i])
            return 0;
    return b[i] == 0;
}

/* TRUE if b[from..to) still holds the sentinel. */
static int untouched(const WCHAR *b, int from, int to) {
    for (int i = from; i < to; i++)
        if (b[i] != SENT)
            return 0;
    return 1;
}

/* The DWORD a LOCALE_RETURN_NUMBER call stores at the start of the buffer (little-endian). */
static DWORD dword_at(const WCHAR *b) {
    const unsigned char *p = (const unsigned char *)b;
    return (DWORD)p[0] | (DWORD)p[1] << 8 | (DWORD)p[2] << 16 | (DWORD)p[3] << 24;
}

TEST(kernel32, GetLocaleInfoW)
{
    WCHAR buf[BUF];

    T_CASE("LOCALE_SNAME as text: en-US and its NUL");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME, buf, BUF), 6);
    T_TRUE(is_text(buf, "en-US"));
    T_TRUE(untouched(buf, 6, BUF));
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME, buf, 6), 6);    /* exactly the size fits */
    T_TRUE(is_text(buf, "en-US"));
    T_TRUE(untouched(buf, 6, BUF));

    T_CASE("LOCALE_SDECIMAL as text: the decimal point");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SDECIMAL, buf, BUF), 2);
    T_TRUE(is_text(buf, "."));
    T_TRUE(untouched(buf, 2, BUF));

    T_CASE("LOCALE_IDEFAULTANSICODEPAGE as text: 1252");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_IDEFAULTANSICODEPAGE, buf, BUF), 5);
    T_TRUE(is_text(buf, "1252"));
    T_TRUE(untouched(buf, 5, BUF));

    T_CASE("LOCALE_IDEFAULTANSICODEPAGE with LOCALE_RETURN_NUMBER: the DWORD 1252 in two WCHARs");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, BUF), 2);
    T_EQ(dword_at(buf), 1252);
    T_TRUE(untouched(buf, 2, BUF));

    T_CASE("the size query returns the length with the NUL and writes nothing");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME, buf, 0), 6);
    T_TRUE(untouched(buf, 0, BUF));
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME, NULL, 0), 6);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_IDEFAULTANSICODEPAGE, NULL, 0), 5);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SDECIMAL, buf, 0), 2);
    T_TRUE(untouched(buf, 0, BUF));

    T_CASE("the size query with LOCALE_RETURN_NUMBER returns 2 and writes nothing");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, 0), 2);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, NULL, 0), 2);
    T_TRUE(untouched(buf, 0, BUF));

    T_CASE("a buffer one too small fails with ERROR_INSUFFICIENT_BUFFER and writes nothing");
    CALL(kernel32, SetLastError, 0);
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME, buf, 5), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(untouched(buf, 0, BUF));
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, 1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(untouched(buf, 0, BUF));

    T_CASE("a negative length fails with ERROR_INVALID_PARAMETER and writes nothing");
    CALL(kernel32, SetLastError, 0);
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME, buf, -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_TRUE(untouched(buf, 0, BUF));
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME, NULL, -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("LOCALE_NOUSEROVERRIDE and LOCALE_USE_CP_ACP change nothing");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME | LOCALE_NOUSEROVERRIDE, buf, BUF), 6);
    T_TRUE(is_text(buf, "en-US"));
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SDECIMAL | LOCALE_USE_CP_ACP, buf, BUF), 2);
    T_TRUE(is_text(buf, "."));
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409,
              LOCALE_IDEFAULTANSICODEPAGE | LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP | LOCALE_RETURN_NUMBER, buf, BUF), 2);
    T_EQ(dword_at(buf), 1252);
    T_TRUE(untouched(buf, 2, BUF));

    T_CASE("locale 0, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT are en-US");
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0, LOCALE_SNAME, buf, BUF), 6);
    T_TRUE(is_text(buf, "en-US"));
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, LOCALE_USER_DEFAULT, LOCALE_SNAME, buf, BUF), 6);
    T_TRUE(is_text(buf, "en-US"));
    fill(buf, BUF);
    T_EQ(CALL(kernel32, GetLocaleInfoW, LOCALE_SYSTEM_DEFAULT, LOCALE_SNAME, buf, BUF), 6);
    T_TRUE(is_text(buf, "en-US"));

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME, buf, BUF), 6);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, BUF), 2);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME, NULL, 0), 6);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: each refused case must end in a fatal report that names it. */
    T_TRAPS(CALL(kernel32, GetLocaleInfoW, 0x0407, LOCALE_SNAME, buf, BUF), "a locale other than en-US");
    T_TRAPS(CALL(kernel32, GetLocaleInfoW, LOCALE_INVARIANT, LOCALE_SNAME, buf, BUF), "a locale other than en-US");
    T_TRAPS(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME | LOCALE_RETURN_GENITIVE_NAMES, buf, BUF),
            "LOCALE_RETURN_GENITIVE_NAMES");
    T_TRAPS(CALL(kernel32, GetLocaleInfoW, 0x0409, LOCALE_SNAME, NULL, BUF), "a NULL buffer with a nonzero length");
}
