#include "rt/test.h"
#include "rt/win/error.h"
#include "rt/win/nls.h"

/* The expected output is written out by hand.  1252: 0x80 is U+20AC and 0xE9 is U+00E9.
 * 437: 0x80 is U+00C7 and 0xDB is U+2588.  UTF-8: C3 A9 is U+00E9, E2 82 AC is U+20AC and
 * F0 9F 98 80 is U+1F600, which is the surrogate pair D83D DE00.  Every WCHAR the function
 * does not write still holds GUARD, so a stray write shows up as a changed element. */
#define GUARD 0xDEADu

static void fill(WCHAR *w, int n)
{
    for (int i = 0; i < n; i++) w[i] = GUARD;
}

/* How many of w[from..to) differ from GUARD. */
static unsigned changed(const WCHAR *w, int from, int to)
{
    unsigned n = 0;
    for (int i = from; i < to; i++) n += w[i] != GUARD;
    return n;
}

TEST(kernel32, MultiByteToWideChar)
{
    WCHAR buf[16], wide[16];
    int r;

    T_CASE("NUL-terminated input with the terminator converted and counted");
    fill(buf, 16);
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, MultiByteToWideChar, 1252, 0, "ab", -1, buf, 16);
    T_EQ(r, 3);
    T_EQ(buf[0], 'a');
    T_EQ(buf[1], 'b');
    T_EQ(buf[2], 0);
    T_EQ(changed(buf, 3, 16), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);               /* success leaves last error alone */

    T_CASE("explicit length with no terminator written");
    fill(buf, 16);
    r = CALL(kernel32, MultiByteToWideChar, 1252, 0, "abc", 2, buf, 16);
    T_EQ(r, 2);
    T_EQ(buf[0], 'a');
    T_EQ(buf[1], 'b');
    T_EQ(changed(buf, 2, 16), 0);

    T_CASE("size query with cchWideChar 0 writes nothing");
    fill(buf, 16);
    r = CALL(kernel32, MultiByteToWideChar, 1252, 0, "ab", -1, buf, 0);
    T_EQ(r, 3);
    T_EQ(changed(buf, 0, 16), 0);
    T_EQ(CALL(kernel32, MultiByteToWideChar, 1252, 0, "ab", 2, NULL, 0), 2);

    T_CASE("code page 1252 maps high bytes");
    fill(buf, 16);
    r = CALL(kernel32, MultiByteToWideChar, 1252, 0, "\x80\xE9" "A", 3, buf, 16);
    T_EQ(r, 3);
    T_EQ(buf[0], 0x20AC);
    T_EQ(buf[1], 0x00E9);
    T_EQ(buf[2], 'A');
    T_EQ(changed(buf, 3, 16), 0);

    T_CASE("code page 437 maps high bytes");
    fill(buf, 16);
    r = CALL(kernel32, MultiByteToWideChar, 437, 0, "\x80\xDB", 2, buf, 16);
    T_EQ(r, 2);
    T_EQ(buf[0], 0x00C7);
    T_EQ(buf[1], 0x2588);
    T_EQ(changed(buf, 2, 16), 0);

    T_CASE("CP_ACP is code page 1252");
    fill(buf, 16);
    r = CALL(kernel32, MultiByteToWideChar, CP_ACP, 0, "\x80\xE9", 2, buf, 16);
    T_EQ(r, 2);
    T_EQ(buf[0], 0x20AC);
    T_EQ(buf[1], 0x00E9);
    T_EQ(changed(buf, 2, 16), 0);

    T_CASE("CP_UTF8 decodes multi-byte sequences and supplementary characters");
    fill(buf, 16);
    r = CALL(kernel32, MultiByteToWideChar, CP_UTF8, 0,
             "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", 9, buf, 16);
    T_EQ(r, 4);
    T_EQ(buf[0], 0x00E9);
    T_EQ(buf[1], 0x20AC);
    T_EQ(buf[2], 0xD83D);
    T_EQ(buf[3], 0xDE00);
    T_EQ(changed(buf, 4, 16), 0);

    T_CASE("buffer one too small gives ERROR_INSUFFICIENT_BUFFER");
    fill(buf, 16);
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, 1252, 0, "abc", -1, buf, 3);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(buf[0], 'a');                          /* as much as fits is written first */
    T_EQ(buf[1], 'b');
    T_EQ(buf[2], 'c');
    T_EQ(changed(buf, 3, 16), 0);

    T_CASE("MB_ERR_INVALID_CHARS on invalid UTF-8 gives ERROR_NO_UNICODE_TRANSLATION");
    fill(buf, 16);
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, CP_UTF8, MB_ERR_INVALID_CHARS, "a\xFF" "b", 3, buf, 16);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_NO_UNICODE_TRANSLATION);
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, CP_UTF8, MB_ERR_INVALID_CHARS, "\xED\xA0\x80", 3, buf, 16);
    T_EQ(r, 0);                                 /* an encoded surrogate */
    T_EQ(T_LAST_ERROR(), ERROR_NO_UNICODE_TRANSLATION);

    T_CASE("bad flags give ERROR_INVALID_FLAGS");
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, 1252, 0x100, "a", 1, buf, 16);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, CP_UTF8, MB_PRECOMPOSED, "a", 1, buf, 16);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    T_TRAPS(CALL(kernel32, MultiByteToWideChar, 1252, MB_COMPOSITE, "a", 1, buf, 16), "MB_COMPOSITE");
    T_TRAPS(CALL(kernel32, MultiByteToWideChar, 437, MB_USEGLYPHCHARS, "a", 1, buf, 16), "MB_USEGLYPHCHARS");

    /* Windows checks the code page before the flags (Wine kernel32/tests/codepage.c,
     * test_other_invalid_parameters: "Unrecognized flag and invalid codepage => ERROR_INVALID_PARAMETER"). */
    T_CASE("a code page Windows does not know gives ERROR_INVALID_PARAMETER, whatever the flags");
    fill(buf, 16);
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, 0xdeadbeef, 0x10, "abc", 3, buf, 8);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(changed(buf, 0, 16), 0);
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, 0xdeadbeef, 0, "abc", 3, buf, 8);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(changed(buf, 0, 16), 0);
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, 1252, 0x10, "abc", 3, buf, 8);
    T_EQ(r, 0);                                 /* a page we carry: its flags are checked */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);

    T_CASE("code page 20127 converts ASCII and fails on a high byte with MB_ERR_INVALID_CHARS");
    fill(buf, 16);
    r = CALL(kernel32, MultiByteToWideChar, 20127, 0, "AZ", 2, buf, 16);
    T_EQ(r, 2);
    T_EQ(buf[0], 'A');
    T_EQ(buf[1], 'Z');
    T_EQ(changed(buf, 2, 16), 0);
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, 20127, MB_ERR_INVALID_CHARS, "a\x80", 2, buf, 16);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_NO_UNICODE_TRANSLATION);
    T_TRAPS(CALL(kernel32, MultiByteToWideChar, 20127, 0, "\x80", 1, buf, 16), "20127");

    T_CASE("invalid parameters give ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, 1252, 0, "a", 0, buf, 16);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    r = CALL(kernel32, MultiByteToWideChar, 1252, 0, (const char *)buf, 1, buf, 16);
    T_EQ(r, 0);                                 /* source and destination are the same pointer */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    fill(wide, 16);
    T_TRAPS(CALL(kernel32, MultiByteToWideChar, 1252, 0, (const char *)wide + 14, 4, wide, 8),
            "overlap");
}
