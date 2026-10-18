#include "rt/test.h"
#include "rt/locale.h"

/* The words are checked against the core's own table, rt_string_type called on the same
 * string (the oracle, as GetTickCount's test uses the core's clock).  Where the documentation
 * says what a flag means, the test also checks that flag by its meaning. */

#define GUARD 0xA5A5u

static void fill(WORD *p, int n, WORD v)
{
    for (int i = 0; i < n; i++)
        p[i] = v;
}

TEST(kernel32, GetStringTypeW)
{
    static const WCHAR s[] = { 'a', 'Z', '5', ' ', 0x01 };
    static const WCHAR A[] = { 'A' };
    static const WCHAR pair[] = { 0xD83D, 0xDE00 };      /* U+1F600: a high and a low surrogate */
    static const WCHAR ab[] = { 'a', 'B', 0, 0x4E00 };   /* the unit after the NUL is never read */
    static const WCHAR abc[] = { 'a', 'b', 0x4E00 };     /* the third unit is never read */
    static const WCHAR spaces[] = { ' ', '\t', 0x00A0 };
    WORD out[8], ref[8];
    BOOL ret;
    int i;

    T_CASE("CT_CTYPE1 for a letter of each case, a digit, a space and a control character");
    fill(out, 8, GUARD);
    ret = CALL(kernel32, GetStringTypeW, CT_CTYPE1, s, 5, out);
    T_EQ(ret, TRUE);
    T_EQ(rt_string_type(CT_CTYPE1, s, 5, ref), TRUE);
    for (i = 0; i < 5; i++)
        T_EQ(out[i], ref[i]);
    for (i = 5; i < 8; i++)
        T_EQ(out[i], GUARD);
    T_TRUE(out[0] & C1_LOWER);                /* 'a' */
    T_TRUE(out[0] & C1_ALPHA);
    T_TRUE(!(out[0] & C1_UPPER));
    T_TRUE(out[1] & C1_UPPER);                /* 'Z' */
    T_TRUE(out[1] & C1_ALPHA);
    T_TRUE(!(out[1] & C1_LOWER));
    T_TRUE(out[2] & C1_DIGIT);                /* '5' */
    T_TRUE(!(out[2] & C1_ALPHA));
    T_TRUE(out[3] & C1_SPACE);                /* ' ' */
    T_TRUE(!(out[3] & C1_ALPHA));
    T_TRUE(out[4] & C1_CNTRL);                /* U+0001 */
    T_TRUE(!(out[4] & C1_ALPHA));
    T_TRUE(!(out[4] & C1_SPACE));
    /* Windows sets C1_DEFINED (0x0200) on every defined character (Wine kernel32/tests/locale.c,
     * test_GetStringTypeW): U+0009 is space, control, blank and defined (0x268); U+0020 and
     * U+00A0 are space, blank and defined (0x248).  A success leaves the last error alone. */
    fill(out, 8, GUARD);
    CALL(kernel32, SetLastError, 0x1234);
    ret = CALL(kernel32, GetStringTypeW, CT_CTYPE1, spaces, 3, out);
    T_EQ(ret, TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(out[0], 0x248);
    T_EQ(out[1], 0x268);
    T_EQ(out[2], 0x248);
    for (i = 3; i < 8; i++)
        T_EQ(out[i], GUARD);

    T_CASE("CT_CTYPE2 and CT_CTYPE3 for one character each");
    fill(out, 8, GUARD);
    ret = CALL(kernel32, GetStringTypeW, CT_CTYPE2, A, 1, out);
    T_EQ(ret, TRUE);
    T_EQ(out[0], C2_LEFTTORIGHT);             /* 'A' is strong left-to-right */
    T_EQ(rt_string_type(CT_CTYPE2, A, 1, ref), TRUE);
    T_EQ(out[0], ref[0]);
    T_EQ(out[1], GUARD);
    fill(out, 8, GUARD);
    ret = CALL(kernel32, GetStringTypeW, CT_CTYPE3, A, 1, out);
    T_EQ(ret, TRUE);
    T_TRUE(out[0] & C3_ALPHA);                /* 'A' is a letter */
    T_EQ(rt_string_type(CT_CTYPE3, A, 1, ref), TRUE);
    T_EQ(out[0], ref[0]);
    T_EQ(out[1], GUARD);

    T_CASE("CT_CTYPE3 of a surrogate unit gives the surrogate flags");
    fill(out, 8, GUARD);
    ret = CALL(kernel32, GetStringTypeW, CT_CTYPE3, pair, 2, out);
    T_EQ(ret, TRUE);
    T_EQ(out[0], C3_HIGHSURROGATE);
    T_EQ(out[1], C3_LOWSURROGATE);
    for (i = 2; i < 8; i++)
        T_EQ(out[i], GUARD);

    T_CASE("a length of -1 writes the NUL and nothing after it");
    fill(out, 8, GUARD);
    ret = CALL(kernel32, GetStringTypeW, CT_CTYPE1, ab, -1, out);
    T_EQ(ret, TRUE);
    T_EQ(rt_string_type(CT_CTYPE1, ab, -1, ref), TRUE);
    for (i = 0; i < 3; i++)
        T_EQ(out[i], ref[i]);
    for (i = 3; i < 8; i++)
        T_EQ(out[i], GUARD);

    T_CASE("a length given writes that many words and no more");
    fill(out, 8, GUARD);
    ret = CALL(kernel32, GetStringTypeW, CT_CTYPE1, abc, 2, out);
    T_EQ(ret, TRUE);
    T_EQ(rt_string_type(CT_CTYPE1, abc, 2, ref), TRUE);
    for (i = 0; i < 2; i++)
        T_EQ(out[i], ref[i]);
    for (i = 2; i < 8; i++)
        T_EQ(out[i], GUARD);

    T_CASE("ERROR_INVALID_FLAGS for a bit outside the three");
    fill(out, 8, GUARD);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeW, CT_CTYPE1 | 0x8, s, 5, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeW, CT_CTYPE2 | 0x10000, s, 5, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    for (i = 0; i < 8; i++)
        T_EQ(out[i], GUARD);                  /* a failure writes nothing */

    T_CASE("ERROR_INVALID_PARAMETER for none or two of the three bits, NULL pointers, length 0 and out equal to src");
    fill(out, 8, GUARD);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeW, 0, s, 5, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeW, CT_CTYPE1 | CT_CTYPE2, s, 5, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeW, CT_CTYPE1 | CT_CTYPE2 | CT_CTYPE3, s, 5, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeW, CT_CTYPE1, NULL, 5, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeW, CT_CTYPE1, NULL, -1, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeW, CT_CTYPE1, s, 5, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeW, CT_CTYPE1, s, 0, out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetStringTypeW, CT_CTYPE1, (LPCWCH)out, 2, (LPWORD)out), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    for (i = 0; i < 8; i++)
        T_EQ(out[i], GUARD);                  /* a failure writes nothing */

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    ret = CALL(kernel32, GetStringTypeW, CT_CTYPE1, s, 5, out);
    T_EQ(ret, TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, GetStringTypeW, CT_CTYPE1, pair, 1, out), "surrogate code unit in CT_CTYPE1 or CT_CTYPE2");
    T_TRAPS(CALL(kernel32, GetStringTypeW, CT_CTYPE2, pair, 2, out), "surrogate code unit in CT_CTYPE1 or CT_CTYPE2");
    T_TRAPS(CALL(kernel32, GetStringTypeW, CT_CTYPE1, abc, 3, out), "code unit the core does not type");
    T_TRAPS(CALL(kernel32, GetStringTypeW, CT_CTYPE3, abc, 3, out), "code unit the core does not type");
}
