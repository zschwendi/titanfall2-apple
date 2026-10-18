#include <string.h>

#include "rt/test.h"

/* Code pages 1252 and 437 have no named constant under rt/win/. */
#define CP_1252  1252u
#define CP_437   437u

#define BUFN   16       /* output buffer size in bytes */
#define GUARD  0xA5     /* bytes the function must not write keep this value */

/* Every byte of buf from `from` on is still the guard value. */
static int untouched(const char *buf, int from)
{
    for (int i = from; i < BUFN; i++)
        if ((unsigned char)buf[i] != GUARD) return 0;
    return 1;
}

TEST(kernel32, WideCharToMultiByte)
{
    /* "AB" and its terminating NUL, the input of most cases */
    static const WCHAR ab[] = { 'A', 'B', 0 };
    /* 'a', U+0101 (not in code page 1252), 'b' */
    static const WCHAR ab_unrep[] = { 'a', 0x0101, 'b' };
    /* 'a', an unpaired high surrogate, 'b' */
    static const WCHAR lone[] = { 'a', 0xD800, 'b' };
    /* U+1F600 as a surrogate pair */
    static const WCHAR pair[] = { 0xD83D, 0xDE00, 0 };
    /* 'a' and its NUL */
    static const WCHAR a_nul[] = { 'a', 0 };
    /* U+0101 and U+4E2D, neither in code page 1252; U+0301, a combining mark */
    static const WCHAR macron[] = { 0x0101 };
    static const WCHAR kanji[] = { 0x4E2D };
    static const WCHAR comb[] = { 0x0301 };
    static const char star = '*';
    char buf[BUFN];
    BOOL used;
    int n;

    T_CASE("-1 and explicit lengths");
    memset(buf, GUARD, BUFN);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, ab, -1, buf, BUFN, NULL, NULL);
    T_EQ(n, 3);
    T_EQ(buf[0], 'A');
    T_EQ(buf[1], 'B');
    T_EQ(buf[2], 0);                    /* -1 includes the NUL in the output */
    T_TRUE(untouched(buf, 3));

    memset(buf, GUARD, BUFN);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, ab, 2, buf, BUFN, NULL, NULL);
    T_EQ(n, 2);
    T_EQ(buf[0], 'A');
    T_EQ(buf[1], 'B');
    T_TRUE(untouched(buf, 2));          /* an explicit length writes no NUL */

    T_CASE("size query");
    memset(buf, GUARD, BUFN);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, ab, -1, NULL, 0, NULL, NULL);
    T_EQ(n, 3);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, ab, 2, NULL, 0, NULL, NULL);
    T_EQ(n, 2);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, ab, -1, buf, 0, NULL, NULL);
    T_EQ(n, 3);
    T_TRUE(untouched(buf, 0));          /* a zero size makes no use of the buffer */

    T_CASE("buffer one too small");
    memset(buf, GUARD, BUFN);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, ab, -1, buf, 2, NULL, NULL);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(buf[0], 'A');                  /* the core writes the whole characters that fit */
    T_EQ(buf[1], 'B');
    T_TRUE(untouched(buf, 2));

    T_CASE("default character and used-default flag");
    /* all four combinations of the two pointers, with WC_NO_BEST_FIT_CHARS */
    memset(buf, GUARD, BUFN);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, WC_NO_BEST_FIT_CHARS, ab_unrep, 3,
             buf, BUFN, NULL, NULL);
    T_EQ(n, 3);
    T_EQ(buf[0], 'a');
    T_EQ(buf[1], '?');                  /* no default given: '?' */
    T_EQ(buf[2], 'b');

    memset(buf, GUARD, BUFN);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, WC_NO_BEST_FIT_CHARS, ab_unrep, 3,
             buf, BUFN, &star, NULL);
    T_EQ(n, 3);
    T_EQ(buf[1], '*');

    memset(buf, GUARD, BUFN);
    used = FALSE;
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, WC_NO_BEST_FIT_CHARS, ab_unrep, 3,
             buf, BUFN, NULL, &used);
    T_EQ(n, 3);
    T_EQ(buf[1], '?');
    T_EQ(used, TRUE);

    memset(buf, GUARD, BUFN);
    used = FALSE;
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, WC_NO_BEST_FIT_CHARS, ab_unrep, 3,
             buf, BUFN, &star, &used);
    T_EQ(n, 3);
    T_EQ(buf[1], '*');
    T_EQ(used, TRUE);

    /* a conversion with nothing to default writes FALSE on success */
    used = TRUE;
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, WC_NO_BEST_FIT_CHARS, ab, -1,
             buf, BUFN, &star, &used);
    T_EQ(n, 3);
    T_EQ(used, FALSE);

    /* a failed call writes no used-default flag */
    used = 77;
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, WC_NO_BEST_FIT_CHARS, ab, 0,
             buf, BUFN, &star, &used);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(used, 77);

    /* without WC_NO_BEST_FIT_CHARS, 1252 applies its best-fit table: U+0101 gives 'a' */
    memset(buf, GUARD, BUFN);
    used = TRUE;
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, macron, 1, buf, BUFN, NULL, &used);
    T_EQ(n, 1);
    T_EQ(buf[0], 'a');
    T_EQ(used, FALSE);

    /* and U+4E2D has no best fit: the default character, flagged */
    memset(buf, GUARD, BUFN);
    used = FALSE;
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, kanji, 1, buf, BUFN, NULL, &used);
    T_EQ(n, 1);
    T_EQ(buf[0], '?');
    T_EQ(used, TRUE);

    /* a character code page 437 lacks, without WC_NO_BEST_FIT_CHARS, stops the core */
    T_TRAPS(CALL(kernel32, WideCharToMultiByte, CP_437, 0, macron, 1, buf, BUFN, NULL, NULL),
            "no direct mapping");

    T_CASE("UTF-8 with default character pointers fails");
    used = 77;
    n = CALL(kernel32, WideCharToMultiByte, CP_UTF8, 0, ab, -1, buf, BUFN, &star, &used);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(used, 77);

    n = CALL(kernel32, WideCharToMultiByte, CP_UTF8, 0, ab, -1, buf, BUFN, &star, NULL);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    n = CALL(kernel32, WideCharToMultiByte, CP_UTF8, 0, ab, -1, buf, BUFN, NULL, &used);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("bad flag");
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0x40000000u, ab, -1, buf, BUFN, NULL, NULL);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);

    n = CALL(kernel32, WideCharToMultiByte, CP_UTF8, WC_NO_BEST_FIT_CHARS, ab, -1, buf, BUFN,
             NULL, NULL);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);

    /* flags the core does not handle stop it, whatever the input */
    T_TRAPS(CALL(kernel32, WideCharToMultiByte, CP_1252, WC_DISCARDNS, a_nul, -1, buf, BUFN,
                 NULL, NULL),
            "WC_DISCARDNS");
    T_TRAPS(CALL(kernel32, WideCharToMultiByte, CP_1252, WC_COMPOSITECHECK, comb, 1, buf, BUFN,
                 NULL, NULL),
            "combining mark");

    /* Windows checks the code page before the flags (Wine kernel32/tests/codepage.c,
     * test_other_invalid_parameters: "Unrecognized flag and invalid codepage => ERROR_INVALID_PARAMETER"). */
    T_CASE("a code page Windows does not know gives ERROR_INVALID_PARAMETER, whatever the flags");
    memset(buf, GUARD, BUFN);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, WideCharToMultiByte, 0xdeadbeefu, 0x100, ab, -1, buf, BUFN, NULL, NULL);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_TRUE(untouched(buf, 0));
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, WideCharToMultiByte, 0xdeadbeefu, 0, ab, -1, buf, BUFN, NULL, NULL);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_TRUE(untouched(buf, 0));
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0x100, ab, -1, buf, BUFN, NULL, NULL);
    T_EQ(n, 0);                         /* a page we carry: its flags are checked */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);

    T_CASE("UTF-8 surrogate pair becomes four bytes");
    memset(buf, GUARD, BUFN);
    n = CALL(kernel32, WideCharToMultiByte, CP_UTF8, 0, pair, 2, buf, BUFN, NULL, NULL);
    T_EQ(n, 4);
    T_EQ((unsigned char)buf[0], 0xF0);
    T_EQ((unsigned char)buf[1], 0x9F);
    T_EQ((unsigned char)buf[2], 0x98);
    T_EQ((unsigned char)buf[3], 0x80);
    T_TRUE(untouched(buf, 4));

    memset(buf, GUARD, BUFN);
    n = CALL(kernel32, WideCharToMultiByte, CP_UTF8, 0, pair, -1, buf, BUFN, NULL, NULL);
    T_EQ(n, 5);                         /* with the NUL */
    T_EQ(buf[4], 0);

    T_CASE("UTF-8 unpaired surrogate becomes U+FFFD");
    memset(buf, GUARD, BUFN);
    n = CALL(kernel32, WideCharToMultiByte, CP_UTF8, 0, lone, 3, buf, BUFN, NULL, NULL);
    T_EQ(n, 5);
    T_EQ(buf[0], 'a');
    T_EQ((unsigned char)buf[1], 0xEF);
    T_EQ((unsigned char)buf[2], 0xBF);
    T_EQ((unsigned char)buf[3], 0xBD);
    T_EQ(buf[4], 'b');
    T_TRUE(untouched(buf, 5));

    T_CASE("WC_ERR_INVALID_CHARS fails on an unpaired surrogate");
    memset(buf, GUARD, BUFN);
    n = CALL(kernel32, WideCharToMultiByte, CP_UTF8, WC_ERR_INVALID_CHARS, lone, 3, buf, BUFN,
             NULL, NULL);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_NO_UNICODE_TRANSLATION);

    T_CASE("invalid arguments fail with ERROR_INVALID_PARAMETER");
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, NULL, -1, buf, BUFN, NULL, NULL);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, ab, 0, buf, BUFN, NULL, NULL);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, ab, -1, buf, -1, NULL, NULL);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    /* the output buffer is the input buffer */
    {
        WCHAR w[] = { 'A', 'B', 0 };
        n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, w, -1, (char *)w, BUFN, NULL, NULL);
        T_EQ(n, 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    }

    /* buffers that overlap but are not identical stop the core */
    {
        WCHAR arr[8] = { 'a', 'b', 'c', 0 };
        T_TRAPS(CALL(kernel32, WideCharToMultiByte, CP_1252, 0, arr, -1, (char *)arr + 1, 8,
                     NULL, NULL),
                "overlap");
    }

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, ab, -1, buf, BUFN, NULL, NULL);
    T_EQ(n, 3);
    n = CALL(kernel32, WideCharToMultiByte, CP_1252, 0, ab, -1, NULL, 0, NULL, NULL);
    T_EQ(n, 3);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
