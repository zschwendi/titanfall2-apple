#include "rt/test.h"
#include "rt/locale.h"

/* The words are checked against the core's own table: rt_string_type called on the wide
 * string that the core's conversion gives (the oracle, as the W test uses the core's table).
 * Code page 1252 maps one byte to one WCHAR, so the counts are the same as in the wide form. */

#define GUARD 0xA5A5u

static void fill(WORD *p, int n, WORD v)
{
    for (int i = 0; i < n; i++)
        p[i] = v;
}

TEST(kernel32, GetStringTypeA)
{
    static const char txt[] = "a1 ";                 /* the NUL is the fourth byte */
    static const WCHAR wide_txt[] = { 'a', '1', ' ', 0 };
    static const char e_acute[] = { (char)0xE9 };
    static const WCHAR e_wide[] = { 0x00E9 };
    static const DWORD locales[] = { 0x0409, 0, LOCALE_INVARIANT, LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT };
    static char big[1100];                          /* 1,100 zero bytes */
    static WORD bigout[1100];
    static char all[256];
    static WCHAR all_wide[256];
    static WCHAR big_wide[1024];
    static WORD all_out[256], all_ref[256];
    WORD out[8], ref[8];
    BOOL ret;
    int i, j;

    T_CASE("CT_CTYPE1 of a1 with a length of 3 gives the words of rt_string_type on the wide string");
    fill(out, 8, GUARD);
    ret = CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1, txt, 3, out);
    T_EQ(ret, TRUE);
    T_EQ(rt_string_type(CT_CTYPE1, wide_txt, 3, ref), TRUE);
    for (i = 0; i < 3; i++)
        T_EQ(out[i], ref[i]);
    for (i = 3; i < 8; i++)
        T_EQ(out[i], GUARD);                  /* no word after the output is written */
    T_TRUE(out[0] & C1_ALPHA);                /* 'a' */
    T_TRUE(out[0] & C1_LOWER);
    T_TRUE(out[1] & C1_DIGIT);                /* '1' */
    T_TRUE(out[2] & C1_SPACE);                /* ' ' */

    T_CASE("a length of -1 also writes the terminator's word");
    fill(out, 8, GUARD);
    ret = CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1, txt, -1, out);
    T_EQ(ret, TRUE);
    T_EQ(rt_string_type(CT_CTYPE1, wide_txt, 4, ref), TRUE);
    for (i = 0; i < 4; i++)
        T_EQ(out[i], ref[i]);
    T_TRUE(out[3] != GUARD);                  /* the word of the NUL is written */
    for (i = 4; i < 8; i++)
        T_EQ(out[i], GUARD);

    T_CASE("every byte of code page 1252 gives the words of rt_string_type on its wide character");
    for (i = 0; i < 256; i++)
        all[i] = (char)i;
    T_EQ(rt_mb_to_wide(CP_ACP, 0, all, 256, all_wide, 256), 256);
    T_EQ(rt_string_type(CT_CTYPE1, all_wide, 256, all_ref), TRUE);
    fill(all_out, 256, GUARD);
    T_EQ(CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1, all, 256, all_out), TRUE);
    for (i = 0; i < 256; i++)
        T_EQ(all_out[i], all_ref[i]);

    T_CASE("byte 0xE9 has the type of U+00E9");
    fill(out, 8, GUARD);
    ret = CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1, e_acute, 1, out);
    T_EQ(ret, TRUE);
    T_EQ(rt_string_type(CT_CTYPE1, e_wide, 1, ref), TRUE);
    T_EQ(out[0], ref[0]);
    T_TRUE(out[0] & C1_ALPHA);                /* e acute is a lower-case letter */
    T_TRUE(out[0] & C1_LOWER);
    T_EQ(out[1], GUARD);

    T_CASE("the locales 0x0409, 0, LOCALE_INVARIANT, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT all mean code page 1252");
    T_EQ(rt_string_type(CT_CTYPE1, wide_txt, 3, ref), TRUE);     /* the 0x0409 words */
    for (i = 0; i < (int)(sizeof locales / sizeof locales[0]); i++) {
        fill(out, 8, GUARD);
        ret = CALL(kernel32, GetStringTypeA, locales[i], CT_CTYPE1, txt, 3, out);
        T_EQ(ret, TRUE);
        for (j = 0; j < 3; j++)
            T_EQ(out[j], ref[j]);
        T_EQ(out[3], GUARD);
    }

    T_CASE("ERROR_INVALID_PARAMETER for info type 0");
    fill(out, 8, GUARD);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeA, 0x0409, 0, txt, 3, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    for (i = 0; i < 8; i++)
        T_EQ(out[i], GUARD);                  /* a failure writes nothing */

    T_CASE("ERROR_INVALID_FLAGS for an info type with a bit outside the three");
    fill(out, 8, GUARD);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1 | 0x8, txt, 3, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    for (i = 0; i < 8; i++)
        T_EQ(out[i], GUARD);                  /* a failure writes nothing */

    T_CASE("ERROR_INVALID_PARAMETER for a NULL source or output, length 0 and out equal to src");
    fill(out, 8, GUARD);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1, NULL, 3, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1, txt, 3, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1, txt, 0, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1, (LPCSTR)out, 2, (LPWORD)out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    for (i = 0; i < 8; i++)
        T_EQ(out[i], GUARD);                  /* a failure writes nothing */

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    ret = CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1, txt, 3, out);
    T_EQ(ret, TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stop for more than 1024 characters is the shim's reading of the core's code for a
     * conversion that does not fit (rt_mb_to_wide, locale.h: ERROR_INSUFFICIENT_BUFFER and 0).
     * The shim cannot reach that code without the stop, so the core's report is checked here. */
    T_EQ(rt_mb_to_wide(CP_ACP, 0, big, 1100, big_wide, 1024), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);

    T_TRAPS(CALL(kernel32, GetStringTypeA, 0x0407, CT_CTYPE1, txt, 3, out), "locale other than 0x0409, 0, LOCALE_INVARIANT, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT (0x407)");
    T_TRAPS(CALL(kernel32, GetStringTypeA, 0x0409, CT_CTYPE1, big, 1100, bigout), "more than 1024 characters");
}
