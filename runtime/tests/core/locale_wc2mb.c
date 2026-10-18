/* Tests of WideCharToMultiByte (rt_wide_to_mb) in runtime/core/locale.c, and round trips with
 * MultiByteToWideChar.  See locale.c in this directory for the oracles and conventions.
 *
 * What the documentation fixes and the tests check: the default character and the used-default
 * flag (all four combinations of the two pointers), the flags per code page, a length of -1
 * including the terminator, a zero output size as the size query, too-small and overlapping
 * buffers, and the exact error codes.  What it leaves open and the core refuses loudly:
 * Windows's best-fit mapping, composition and the decomposition flags.
 */
#include <limits.h>
#include <string.h>
#include <sys/mman.h>

#include "rt/locale.h"
#include "rt/test.h"

extern const uint16_t locale_exp_cp1252[256], locale_exp_cp437[256];
extern const uint8_t locale_exp_cp1252_undefined[5];

#define SENTINEL 0x7A5A5A01u
static void set_err(void) { rt_set_last_error(T_CTX(), SENTINEL); }
#define FAILS(call, err)  do { set_err(); T_EQ((call), 0); T_EQ(T_LAST_ERROR(), (err)); } while (0)
#define OK_N(call, n)     do { set_err(); T_EQ((call), (n)); T_EQ(T_LAST_ERROR(), SENTINEL); } while (0)

static void fill_a(char *p, int n) { memset(p, 0xA5, (size_t)n); }
static bool untouched_a(const char *p, int n) { for (int i = 0; i < n; i++) if ((uint8_t)p[i] != 0xA5) return false; return true; }

/* A wide string literal as WCHARs. */
#define WSTR(lit)  ((const WCHAR *)u##lit)

/* The table of a single-byte page, as Windows has it. */
static const uint16_t *table_of(UINT cp, uint16_t *scratch)
{
    switch (cp) {
    case 1252: return locale_exp_cp1252;
    case 437:  return locale_exp_cp437;
    case 28591: for (int i = 0; i < 256; i++) scratch[i] = (uint16_t)i; return scratch;
    default:   for (int i = 0; i < 256; i++) scratch[i] = i < 128 ? (uint16_t)i : 0xFFFF; return scratch;     /* 20127 */
    }
}

/* ============================================================================================
 * Single-byte code pages
 * ========================================================================================== */

TEST(core, locale_wc2mb_every_character_of_each_page)
{
    static const UINT pages[] = { 1252, 437, 28591, 20127 };
    for (unsigned p = 0; p < 4; p++) {
        uint16_t scratch[256];
        const uint16_t *tab = table_of(pages[p], scratch);
        /* A byte -> wide -> byte round trip for each of the 256 bytes (128 for ASCII), alone and
         * all in one call, with and without WC_NO_BEST_FIT_CHARS. */
        WCHAR all[256];
        char back[256];
        int n = pages[p] == 20127 ? 128 : 256;
        for (int b = 0; b < n; b++) all[b] = tab[b];
        static const DWORD flag_sets[] = { 0, WC_NO_BEST_FIT_CHARS, WC_COMPOSITECHECK, WC_COMPOSITECHECK | WC_NO_BEST_FIT_CHARS };
        for (unsigned f = 0; f < 4; f++) {
            fill_a(back, 256);
            BOOL used = TRUE;
            OK_N(rt_wide_to_mb(pages[p], flag_sets[f], all, n, back, 256, NULL, &used), n);
            T_EQ(used, FALSE);                                 /* written on success, even when FALSE */
            for (int b = 0; b < n; b++) T_EQ((uint8_t)back[b], b);
            T_TRUE(untouched_a(back + n, 256 - n));
            for (int b = 0; b < n; b++) {
                char one = 0;
                used = TRUE;
                OK_N(rt_wide_to_mb(pages[p], flag_sets[f], &all[b], 1, &one, 1, NULL, &used), 1);
                T_EQ((uint8_t)one, b);
                T_EQ(used, FALSE);
            }
        }
    }
    /* Windows 1252 round-trips the five undefined bytes through the same-valued controls. */
    for (int i = 0; i < 5; i++) {
        WCHAR w = locale_exp_cp1252_undefined[i];
        char c = 0;
        BOOL used = TRUE;
        OK_N(rt_wide_to_mb(1252, WC_NO_BEST_FIT_CHARS, &w, 1, &c, 1, NULL, &used), 1);
        T_EQ((uint8_t)c, w);
        T_EQ(used, FALSE);
    }
    /* The special code page values. */
    char c;
    WCHAR euro = 0x20AC;
    OK_N(rt_wide_to_mb(CP_ACP, 0, &euro, 1, &c, 1, NULL, NULL), 1);
    T_EQ((uint8_t)c, 0x80);
    OK_N(rt_wide_to_mb(CP_THREAD_ACP, 0, &euro, 1, &c, 1, NULL, NULL), 1);
    T_EQ((uint8_t)c, 0x80);
    WCHAR c7 = 0x00C7;
    OK_N(rt_wide_to_mb(CP_OEMCP, 0, &c7, 1, &c, 1, NULL, NULL), 1);
    T_EQ((uint8_t)c, 0x80);
}

TEST(core, locale_wc2mb_unrepresentable_characters)
{
    /* Every UTF-16 unit, against every page, with WC_NO_BEST_FIT_CHARS: the byte of the page if
     * it has one, else the default character and *used_default.  (262,144 calls.) */
    static const UINT pages[] = { 1252, 437, 28591, 20127 };
    for (unsigned p = 0; p < 4; p++) {
        uint16_t scratch[256];
        const uint16_t *tab = table_of(pages[p], scratch);
        int direct[65536];
        for (int w = 0; w < 65536; w++) direct[w] = -1;
        for (int b = 0; b < 256; b++) if (tab[b] != 0xFFFF) direct[tab[b]] = b;
        unsigned bad = 0;
        for (int w = 0; w < 65536 && bad < 10; w++) {
            WCHAR u = (WCHAR)w;
            char out = 0;
            BOOL used = 7;
            int r = rt_wide_to_mb(pages[p], WC_NO_BEST_FIT_CHARS, &u, 1, &out, 1, NULL, &used);
            int want = direct[w] >= 0 ? direct[w] : '?';
            BOOL want_used = direct[w] < 0;
            if (r != 1 || (uint8_t)out != want || used != want_used) {
                bad++;
                rt_test_fail(__FILE__, __LINE__, "code page %u U+%04X: got %d byte %#x used %d, expected byte %#x used %d", pages[p], w, r,
                             (uint8_t)out, used, want, want_used);
            }
        }
    }
    /* The default character: the caller's, or '?'; *used_default for all four pointer combinations. */
    WCHAR bad = 0x20AC;                                       /* in 1252, not in 437 */
    char out;
    BOOL used;
    char x = 'X';
    used = FALSE;
    OK_N(rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, &bad, 1, &out, 1, NULL, &used), 1);
    T_EQ(out, '?'); T_EQ(used, TRUE);
    used = FALSE;
    OK_N(rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, &bad, 1, &out, 1, &x, &used), 1);
    T_EQ(out, 'X'); T_EQ(used, TRUE);
    out = 0;
    OK_N(rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, &bad, 1, &out, 1, &x, NULL), 1);       /* default given, flag not asked for */
    T_EQ(out, 'X');
    out = 0;
    OK_N(rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, &bad, 1, &out, 1, NULL, NULL), 1);    /* neither */
    T_EQ(out, '?');
    char zero = 0;
    OK_N(rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, &bad, 1, &out, 1, &zero, &used), 1);  /* NUL is a legal default */
    T_EQ(out, 0);
    char two[2] = { 'Q', 'R' };
    OK_N(rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, &bad, 1, &out, 1, two, &used), 1);    /* single-byte page: first byte only */
    T_EQ(out, 'Q');
    /* The flag is FALSE when nothing was replaced, even if it was TRUE before; the default
     * character that is itself in the string is not "used". */
    used = TRUE;
    OK_N(rt_wide_to_mb(1252, WC_NO_BEST_FIT_CHARS, WSTR("a?b"), 3, (char[4]){0}, 4, NULL, &used), 3);
    T_EQ(used, FALSE);
    /* Several characters, one of them unrepresentable: TRUE, and the rest converted. */
    char buf[8];
    OK_N(rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, WSTR("a\u20ACb"), 3, buf, 8, NULL, &used), 3);
    T_TRUE(!memcmp(buf, "a?b", 3));
    T_EQ(used, TRUE);
    /* A surrogate pair is two unrepresentable units (two default characters; not verified for Windows). */
    OK_N(rt_wide_to_mb(1252, WC_NO_BEST_FIT_CHARS, WSTR("\U0001F600"), 2, buf, 8, NULL, &used), 2);
    T_TRUE(!memcmp(buf, "??", 2));
    T_EQ(used, TRUE);
    OK_N(rt_wide_to_mb(1252, WC_NO_BEST_FIT_CHARS, WSTR("\xD83D"), 1, buf, 8, NULL, &used), 1);    /* a lone surrogate too */
    /* The size query computes the flag as well. */
    used = FALSE;
    OK_N(rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, WSTR("a\u20ACb"), 3, NULL, 0, NULL, &used), 3);
    T_EQ(used, TRUE);
    /* It is not written when the call fails. */
    used = 7;
    FAILS(rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, WSTR("a\u20ACb"), 3, buf, 2, NULL, &used), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(used, 7);
    FAILS(rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, NULL, 3, buf, 2, NULL, &used), ERROR_INVALID_PARAMETER);
    T_EQ(used, 7);
}

TEST(core, locale_wc2mb_best_fit_1252)
{
    /* Without WC_NO_BEST_FIT_CHARS Windows substitutes from its best-fit table (bestfit1252.txt).
     * A substitution is not a use of the default character; a character the table lacks is. */
    char out[8];
    BOOL used = 7;
    memset(out, 1, sizeof out);
    OK_N(rt_wide_to_mb(1252, 0, WSTR("a\u0101\u0141\uFF21\u212B\u2212"), 6, out, 8, NULL, &used), 6);
    T_EQ(out[0], 'a');
    T_EQ(out[1], 'a');                      /* a with macron */
    T_EQ(out[2], 'L');                      /* L with stroke */
    T_EQ(out[3], 'A');                      /* fullwidth A */
    T_EQ((unsigned char)out[4], 0xC5);      /* angstrom sign: A with ring */
    T_EQ(out[5], '-');                      /* minus sign */
    T_EQ(out[6], 1);
    T_EQ(used, FALSE);
    /* The size query counts the same. */
    OK_N(rt_wide_to_mb(1252, 0, WSTR("a\u0101"), 2, NULL, 0, NULL, NULL), 2);
    /* The two the C runtime's start-up converts with flags 0: U+0191 has a best fit, 0x83 (the
     * small f with hook), and U+039C has none and gets the default character.  Only the second
     * is a use of the default. */
    {
        char two[4] = { 1, 1, 1, 1 };
        used = 7;
        OK_N(rt_wide_to_mb(1252, 0, WSTR("\u0191"), 1, two, 4, NULL, &used), 1);
        T_EQ((unsigned char)two[0], 0x83);
        T_EQ(used, FALSE);
        OK_N(rt_wide_to_mb(1252, 0, WSTR("\u0191\u039C"), 2, two, 4, NULL, &used), 2);
        T_EQ((unsigned char)two[0], 0x83);
        T_EQ(two[1], '?');
        T_EQ(used, TRUE);
        OK_N(rt_wide_to_mb(1252, 0, WSTR("\u039C"), 1, two, 4, "*", NULL), 1);
        T_EQ(two[0], '*');
        /* A character of no table at all (a CJK ideograph) is the default character too. */
        used = 7;
        OK_N(rt_wide_to_mb(1252, 0, WSTR("\u4E2D"), 1, two, 4, NULL, &used), 1);
        T_EQ(two[0], '?');
        T_EQ(used, TRUE);
        /* So are U+FFFD through CP_ACP, a character with WC_COMPOSITECHECK set that is no
         * combining mark, and each half of a surrogate pair. */
        used = 7;
        OK_N(rt_wide_to_mb(CP_ACP, 0, WSTR("\uFFFD"), 1, two, 4, NULL, &used), 1);
        T_EQ(two[0], '?');
        T_EQ(used, TRUE);
        OK_N(rt_wide_to_mb(1252, WC_COMPOSITECHECK, WSTR("\u4E00"), 1, two, 4, NULL, NULL), 1);
        T_EQ(two[0], '?');
        two[2] = 1;
        OK_N(rt_wide_to_mb(1252, 0, WSTR("\xD83D\xDE00"), 2, two, 4, NULL, NULL), 2);
        T_EQ(two[0], '?');
        T_EQ(two[1], '?');
        T_EQ(two[2], 1);
        /* With the flag there is no best fit: every one gets the default character. */
        OK_N(rt_wide_to_mb(1252, WC_NO_BEST_FIT_CHARS, WSTR("\u0191"), 1, two, 4, NULL, &used), 1);
        T_EQ(two[0], '?');
        T_EQ(used, TRUE);
        used = 7;
        OK_N(rt_wide_to_mb(1252, WC_NO_BEST_FIT_CHARS, WSTR("\u0101"), 1, two, 4, NULL, &used), 1);
        T_EQ(two[0], '?');
        T_EQ(used, TRUE);
    }
}

TEST(core, locale_wc2mb_best_fit_of_other_pages_is_refused)
{
    /* Only code page 1252 has its best-fit table here.  On another single-byte page an
     * unrepresentable character without WC_NO_BEST_FIT_CHARS is fatal, and the report names it. */
    char out[8];
    T_TRAPS(rt_wide_to_mb(437, 0, WSTR("\u0191"), 1, out, 8, NULL, NULL), "U+0191 at index 0 has no direct mapping in code page 437");
    T_TRAPS(rt_wide_to_mb(437, 0, WSTR("\u20AC"), 1, NULL, 0, NULL, NULL), "U+20AC at index 0 has no direct mapping in code page 437");
    T_TRAPS(rt_wide_to_mb(28591, 0, WSTR("\u0100"), 1, out, 8, NULL, NULL), "code page 28591");
    T_TRAPS(rt_wide_to_mb(20127, 0, WSTR("\u00E9"), 1, out, 8, NULL, NULL), "U+00E9");
    T_TRAPS(rt_wide_to_mb(437, 0, WSTR("\xD83D\xDE00"), 2, out, 8, NULL, NULL), "U+D83D at index 0");
    /* Representable text never traps, whatever the flags. */
    OK_N(rt_wide_to_mb(1252, 0, WSTR("caf\u00E9 \u20AC5"), 7, out, 8, NULL, NULL), 7);
    OK_N(rt_wide_to_mb(437, 0, WSTR("caf\u00E9"), 4, out, 8, NULL, NULL), 4);
}

TEST(core, locale_wc2mb_flags)
{
    char out[8];
    static const UINT sb[] = { 1252, 437, 28591, 20127, CP_ACP, CP_OEMCP, CP_THREAD_ACP };
    for (unsigned i = 0; i < sizeof sb / sizeof sb[0]; i++) {
        OK_N(rt_wide_to_mb(sb[i], 0, WSTR("abc"), 3, out, 8, NULL, NULL), 3);
        OK_N(rt_wide_to_mb(sb[i], WC_NO_BEST_FIT_CHARS, WSTR("abc"), 3, out, 8, NULL, NULL), 3);
        OK_N(rt_wide_to_mb(sb[i], WC_COMPOSITECHECK, WSTR("abc"), 3, out, 8, NULL, NULL), 3);
        OK_N(rt_wide_to_mb(sb[i], WC_COMPOSITECHECK | WC_NO_BEST_FIT_CHARS, WSTR("abc"), 3, out, 8, NULL, NULL), 3);
        /* WC_ERR_INVALID_CHARS is UTF-8 only, and bits that are no WC_ flag are invalid. */
        static const DWORD junk[] = { WC_ERR_INVALID_CHARS, 0x1, 0x2, 0x4, 0x8, 0x100, 0x800, 0x1000, 0x80000000u, 0xFFFFFFFFu,
                                      WC_ERR_INVALID_CHARS | WC_NO_BEST_FIT_CHARS, 0x10000 };
        for (unsigned j = 0; j < sizeof junk / sizeof junk[0]; j++) FAILS(rt_wide_to_mb(sb[i], junk[j], WSTR("abc"), 3, out, 8, NULL, NULL), ERROR_INVALID_FLAGS);
        /* The composition sub-flags are valid in Windows (with WC_COMPOSITECHECK); the core does
         * not implement them, and says so. */
        T_TRAPS(rt_wide_to_mb(sb[i], WC_COMPOSITECHECK | WC_DISCARDNS, WSTR("abc"), 3, out, 8, NULL, NULL), "WC_DISCARDNS");
        T_TRAPS(rt_wide_to_mb(sb[i], WC_COMPOSITECHECK | WC_SEPCHARS, WSTR("abc"), 3, out, 8, NULL, NULL), "WC_SEPCHARS");
        T_TRAPS(rt_wide_to_mb(sb[i], WC_COMPOSITECHECK | WC_DEFAULTCHAR | WC_NO_BEST_FIT_CHARS, WSTR("abc"), 3, out, 8, NULL, NULL), "WC_DEFAULTCHAR");
        T_TRAPS(rt_wide_to_mb(sb[i], WC_DISCARDNS, WSTR("abc"), 3, out, 8, NULL, NULL), "WC_DISCARDNS");
        T_TRAPS(rt_wide_to_mb(sb[i], WC_SEPCHARS, WSTR("abc"), 3, NULL, 0, NULL, NULL), "flags 0x20");
        T_TRAPS(rt_wide_to_mb(sb[i], WC_DEFAULTCHAR, WSTR("abc"), 3, out, 8, NULL, NULL), "WC_DEFAULTCHAR");
        /* WC_COMPOSITECHECK with a combining mark would need composition tables. */
        T_TRAPS(rt_wide_to_mb(sb[i], WC_COMPOSITECHECK, WSTR("e\u0301"), 2, out, 8, NULL, NULL), "combining mark U+0301 at index 1");
        T_TRAPS(rt_wide_to_mb(sb[i], WC_COMPOSITECHECK | WC_NO_BEST_FIT_CHARS, WSTR("\u0300"), 1, out, 8, NULL, NULL), "U+0300");
        T_TRAPS(rt_wide_to_mb(sb[i], WC_COMPOSITECHECK, WSTR("a\u036F"), 2, out, 8, NULL, NULL), "U+036F");
        /* A mark without the flag is just an unrepresentable character. */
        OK_N(rt_wide_to_mb(sb[i], WC_NO_BEST_FIT_CHARS, WSTR("e\u0301"), 2, out, 8, NULL, NULL), 2);
        T_EQ(out[1], '?');
    }
    /* UTF-8: 0 and WC_ERR_INVALID_CHARS only. */
    OK_N(rt_wide_to_mb(CP_UTF8, 0, WSTR("abc"), 3, out, 8, NULL, NULL), 3);
    OK_N(rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, WSTR("abc"), 3, out, 8, NULL, NULL), 3);
    static const DWORD u8bad[] = { WC_NO_BEST_FIT_CHARS, WC_COMPOSITECHECK, WC_DISCARDNS, WC_SEPCHARS, WC_DEFAULTCHAR,
                                   WC_ERR_INVALID_CHARS | WC_NO_BEST_FIT_CHARS, 0x1, 0x80000000u, 0xFFFFFFFFu, 0x100 };
    for (unsigned j = 0; j < sizeof u8bad / sizeof u8bad[0]; j++) FAILS(rt_wide_to_mb(CP_UTF8, u8bad[j], WSTR("abc"), 3, out, 8, NULL, NULL), ERROR_INVALID_FLAGS);

    /* A code page we do not convert: checked before the flags, so ERROR_INVALID_PARAMETER and one log line,
     * bad flags or not.  For a real Windows page among them (932) that differs from Windows only when the
     * flags are bad too; see the order below. */
    static const UINT unsup[] = { CP_MACCP, CP_SYMBOL, CP_UTF7, 850, 932, 1250, 1200, 12000, 54936, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof unsup / sizeof unsup[0]; i++) {
        unsigned before = T_LOG_COUNT(RT_CH_IMPORT);
        FAILS(rt_wide_to_mb(unsup[i], 0, WSTR("abc"), 3, out, 8, NULL, NULL), ERROR_INVALID_PARAMETER);
        T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before + 1);
        FAILS(rt_wide_to_mb(unsup[i], WC_NO_BEST_FIT_CHARS, WSTR("abc"), 3, NULL, 0, NULL, NULL), ERROR_INVALID_PARAMETER);
        FAILS(rt_wide_to_mb(unsup[i], 0x1, WSTR("abc"), 3, out, 8, NULL, NULL), ERROR_INVALID_PARAMETER);
        T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before + 3);
    }
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "WideCharToMultiByte: code page 932 is not supported"));
}

TEST(core, locale_wc2mb_lengths_and_buffers)
{
    char out[16];
    /* -1: the NUL is converted and counted. */
    fill_a(out, 16);
    OK_N(rt_wide_to_mb(1252, 0, WSTR("abc"), -1, out, 16, NULL, NULL), 4);
    T_TRUE(!memcmp(out, "abc\0", 4)); T_TRUE(untouched_a(out + 4, 12));
    fill_a(out, 16);
    OK_N(rt_wide_to_mb(1252, 0, WSTR(""), -1, out, 16, NULL, NULL), 1);
    T_EQ(out[0], 0); T_TRUE(untouched_a(out + 1, 15));
    /* Any negative length means NUL-terminated. */
    OK_N(rt_wide_to_mb(1252, 0, WSTR("xy"), -2, out, 16, NULL, NULL), 3);
    OK_N(rt_wide_to_mb(437, 0, WSTR("xy"), INT_MIN, out, 16, NULL, NULL), 3);
    OK_N(rt_wide_to_mb(CP_UTF8, 0, WSTR("xy"), -9, out, 16, NULL, NULL), 3);
    /* An explicit length converts that many and writes no NUL; embedded NULs are data. */
    fill_a(out, 16);
    OK_N(rt_wide_to_mb(1252, 0, WSTR("abcdef"), 3, out, 16, NULL, NULL), 3);
    T_TRUE(!memcmp(out, "abc", 3)); T_TRUE(untouched_a(out + 3, 13));
    fill_a(out, 16);
    OK_N(rt_wide_to_mb(1252, 0, WSTR("a\0b"), 3, out, 16, NULL, NULL), 3);
    T_TRUE(!memcmp(out, "a\0b", 3));
    OK_N(rt_wide_to_mb(1252, 0, WSTR("a\0b"), -1, out, 16, NULL, NULL), 2);
    /* The size query. */
    OK_N(rt_wide_to_mb(1252, 0, WSTR("abc"), -1, NULL, 0, NULL, NULL), 4);
    OK_N(rt_wide_to_mb(1252, 0, WSTR("abc"), 3, NULL, 0, NULL, NULL), 3);
    OK_N(rt_wide_to_mb(1252, 0, WSTR("abc"), 3, out, 0, NULL, NULL), 3);
    OK_N(rt_wide_to_mb(CP_UTF8, 0, WSTR("\u00E9\u20AC\U0001F600"), 4, NULL, 0, NULL, NULL), 2 + 3 + 4);
    OK_N(rt_wide_to_mb(CP_UTF8, 0, WSTR("\u00E9"), -1, NULL, 0, NULL, NULL), 3);
    /* Exact fit; one short writes the part that fits and fails. */
    fill_a(out, 16);
    OK_N(rt_wide_to_mb(1252, 0, WSTR("abcd"), 4, out, 4, NULL, NULL), 4);
    T_TRUE(untouched_a(out + 4, 12));
    fill_a(out, 16);
    FAILS(rt_wide_to_mb(1252, 0, WSTR("abcd"), 4, out, 3, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(!memcmp(out, "abc", 3)); T_TRUE(untouched_a(out + 3, 13));
    fill_a(out, 16);
    FAILS(rt_wide_to_mb(437, 0, WSTR("abcd"), -1, out, 4, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);   /* the NUL does not fit */
    T_TRUE(!memcmp(out, "abcd", 4)); T_TRUE(untouched_a(out + 4, 12));
    OK_N(rt_wide_to_mb(437, 0, WSTR("abcd"), -1, out, 5, NULL, NULL), 5);
    fill_a(out, 16);
    FAILS(rt_wide_to_mb(28591, 0, WSTR("ab"), 2, out, 1, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(out[0], 'a'); T_TRUE(untouched_a(out + 1, 15));
    FAILS(rt_wide_to_mb(20127, 0, WSTR("ab"), 2, out, 1, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
    FAILS(rt_wide_to_mb(CP_UTF8, 0, WSTR("ab"), 2, out, 1, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
}

TEST(core, locale_wc2mb_maximum_lengths)
{
    /* A claimed length of INT_MAX is a size: with a small destination the call stops at the part
     * that fits without reading the rest of the claimed source (a guard page follows it). */
    long pg = 16384;
    char *map = mmap(NULL, 2 * (size_t)pg, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    T_REQUIRE(map != MAP_FAILED);
    T_EQ(mprotect(map + pg, (size_t)pg, PROT_NONE), 0);
    WCHAR *src = (WCHAR *)(void *)(map + pg - 8);          /* 4 readable WCHARs, then the guard page */
    src[0] = 'a'; src[1] = 'b'; src[2] = 'c'; src[3] = 'd';
    char out[8];
    fill_a(out, 8);
    FAILS(rt_wide_to_mb(1252, 0, src, INT_MAX, out, 4, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(!memcmp(out, "abcd", 4)); T_TRUE(untouched_a(out + 4, 4));
    FAILS(rt_wide_to_mb(437, 0, src, INT_MAX, out, 4, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
    FAILS(rt_wide_to_mb(CP_UTF8, 0, src, INT_MAX, out, 4, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
    FAILS(rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, src, INT_MAX, out, 1, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
    munmap(map, 2 * (size_t)pg);

    /* The largest destination: INT_MAX bytes claimed, a few used. */
    char big[8];
    OK_N(rt_wide_to_mb(1252, 0, WSTR("abc"), 3, big, INT_MAX, NULL, NULL), 3);
    OK_N(rt_wide_to_mb(CP_UTF8, 0, WSTR("abc"), 3, big, INT_MAX, NULL, NULL), 3);
}

TEST(core, locale_wc2mb_parameters)
{
    char out[8];
    static const UINT cps[] = { 1252, 437, 20127, 28591, CP_UTF8, CP_ACP, CP_OEMCP, CP_THREAD_ACP };
    fill_a(out, 8);
    for (unsigned i = 0; i < sizeof cps / sizeof cps[0]; i++) {
        UINT cp = cps[i];
        FAILS(rt_wide_to_mb(cp, 0, NULL, 3, out, 8, NULL, NULL), ERROR_INVALID_PARAMETER);       /* source NULL */
        FAILS(rt_wide_to_mb(cp, 0, NULL, -1, out, 8, NULL, NULL), ERROR_INVALID_PARAMETER);
        FAILS(rt_wide_to_mb(cp, 0, WSTR("abc"), 0, out, 8, NULL, NULL), ERROR_INVALID_PARAMETER);  /* source length 0 */
        FAILS(rt_wide_to_mb(cp, 0, WSTR("abc"), 0, NULL, 0, NULL, NULL), ERROR_INVALID_PARAMETER);
        FAILS(rt_wide_to_mb(cp, 0, WSTR("abc"), 3, out, -1, NULL, NULL), ERROR_INVALID_PARAMETER); /* output size negative */
        FAILS(rt_wide_to_mb(cp, 0, WSTR("abc"), 3, out, INT_MIN, NULL, NULL), ERROR_INVALID_PARAMETER);
        FAILS(rt_wide_to_mb(cp, 0, WSTR("abc"), 3, NULL, 1, NULL, NULL), ERROR_INVALID_PARAMETER); /* NULL output, nonzero size */
        FAILS(rt_wide_to_mb(cp, 0, WSTR("abc"), 3, NULL, 8, NULL, NULL), ERROR_INVALID_PARAMETER);
        T_TRUE(untouched_a(out, 8));
    }
    /* The same pointer for both. */
    WCHAR same[8] = { 'h', 'i', 0 };
    FAILS(rt_wide_to_mb(1252, 0, same, 3, (char *)(void *)same, 8, NULL, NULL), ERROR_INVALID_PARAMETER);
    FAILS(rt_wide_to_mb(CP_UTF8, 0, same, -1, (char *)(void *)same, 8, NULL, NULL), ERROR_INVALID_PARAMETER);
    /* UTF-8 has no default character: giving either pointer is ERROR_INVALID_PARAMETER. */
    char dc = '?';
    BOOL used;
    FAILS(rt_wide_to_mb(CP_UTF8, 0, WSTR("abc"), 3, out, 8, &dc, NULL), ERROR_INVALID_PARAMETER);
    FAILS(rt_wide_to_mb(CP_UTF8, 0, WSTR("abc"), 3, out, 8, NULL, &used), ERROR_INVALID_PARAMETER);
    FAILS(rt_wide_to_mb(CP_UTF8, 0, WSTR("abc"), 3, out, 8, &dc, &used), ERROR_INVALID_PARAMETER);
    FAILS(rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, WSTR("abc"), 3, NULL, 0, &dc, NULL), ERROR_INVALID_PARAMETER);
    FAILS(rt_wide_to_mb(CP_UTF8, 0, WSTR("abc"), 3, NULL, 0, NULL, &used), ERROR_INVALID_PARAMETER);
    /* Order (NT source order, not verified for Windows 7): parameters, UTF-8 default characters, code page,
     * flags.  932 is a real Windows code page that we do not carry: Windows checks its flags first, so for
     * 932 the answer differs from Windows only when the flags are bad too (Wine kernel32/tests/codepage.c,
     * test_other_invalid_parameters).  0xdeadbeef is not a code page on Windows either. */
    FAILS(rt_wide_to_mb(932, 0x1, NULL, 3, out, 8, NULL, NULL), ERROR_INVALID_PARAMETER);
    FAILS(rt_wide_to_mb(CP_UTF8, 0x1, WSTR("abc"), 3, out, 8, &dc, NULL), ERROR_INVALID_PARAMETER);
    FAILS(rt_wide_to_mb(CP_UTF8, 0x1, WSTR("abc"), 3, out, 8, NULL, NULL), ERROR_INVALID_FLAGS);
    FAILS(rt_wide_to_mb(932, 0x1, WSTR("abc"), 3, out, 8, NULL, NULL), ERROR_INVALID_PARAMETER);    /* the code page beats bad flags (Windows: ERROR_INVALID_FLAGS) */
    FAILS(rt_wide_to_mb(0xdeadbeef, 0x100, WSTR("abc"), 3, out, 8, NULL, NULL), ERROR_INVALID_PARAMETER);
    FAILS(rt_wide_to_mb(0xdeadbeef, 0, WSTR("abc"), 3, out, 8, NULL, NULL), ERROR_INVALID_PARAMETER);
    FAILS(rt_wide_to_mb(1252, 0x100, WSTR("abc"), 3, out, 8, NULL, NULL), ERROR_INVALID_FLAGS);      /* a page we carry: its flags are checked */
    /* The default character arguments are not looked at for a single-byte page when unused, and may be
     * given for the special code page values. */
    OK_N(rt_wide_to_mb(CP_ACP, 0, WSTR("abc"), 3, out, 8, &dc, &used), 3);
    T_EQ(used, FALSE);
}

static void reset_overlap_buffer(uint8_t *buf)
{
    memset(buf, 0, 64);
    for (int i = 0; i < 20; i++) ((WCHAR *)(void *)buf)[i] = 'a';
}

TEST(core, locale_wc2mb_overlap)
{
    uint8_t buf[64];
    WCHAR *wb = (WCHAR *)(void *)buf;
    char *cb = (char *)buf;
    reset_overlap_buffer(buf);
    /* Partial overlap: fatal. */
    T_TRAPS(rt_wide_to_mb(1252, 0, wb, 4, cb + 2, 4, NULL, NULL), "buffers overlap");           /* dst [2,6) inside src [0,8) */
    T_TRAPS(rt_wide_to_mb(1252, 0, wb + 2, 4, cb, 6, NULL, NULL), "buffers overlap");           /* dst [0,6) reaches src [4,12) */
    T_TRAPS(rt_wide_to_mb(CP_UTF8, 0, wb + 1, 4, cb + 9, 4, NULL, NULL), "buffers overlap");     /* src [2,10), dst [9,13) */
    T_TRAPS(rt_wide_to_mb(437, 0, wb, 1, cb + 1, 4, NULL, NULL), "buffers overlap");            /* src [0,2), dst [1,5) */
    /* Disjoint, including adjacent: fine. */
    reset_overlap_buffer(buf);
    OK_N(rt_wide_to_mb(1252, 0, wb + 4, 4, cb, 8, NULL, NULL), 4);                              /* dst [0,8), src [8,16) */
    reset_overlap_buffer(buf);
    OK_N(rt_wide_to_mb(1252, 0, wb, 4, cb + 8, 8, NULL, NULL), 4);                              /* src [0,8), dst [8,16) */
    reset_overlap_buffer(buf);
    OK_N(rt_wide_to_mb(1252, 0, wb, 4, cb + 8, 0, NULL, NULL), 4);                              /* no destination use: nothing to overlap */
    OK_N(rt_wide_to_mb(1252, 0, wb, 4, cb + 1, 0, NULL, NULL), 4);
}

TEST(core, locale_wc2mb_traps_name_the_value)
{
    char out[4];
    T_TRAPS(rt_wide_to_mb(437, 0, WSTR("\u0101"), 1, out, 4, NULL, NULL), "FATAL unimplemented: WideCharToMultiByte: U+0101 at index 0");
    T_TRAPS(rt_wide_to_mb(437, WC_DISCARDNS, WSTR("a"), 1, out, 4, NULL, NULL), "WideCharToMultiByte: flags 0x10 include WC_DISCARDNS");
    T_TRAPS(rt_wide_to_mb(1252, WC_COMPOSITECHECK, WSTR("\u0301"), 1, out, 4, NULL, NULL), "WC_COMPOSITECHECK with the combining mark U+0301 at index 0 (code page 1252)");
}

/* ============================================================================================
 * UTF-8 encoding
 * ========================================================================================== */

/* A reference encoder, the textbook way, for the exhaustive round trip. */
static int ref_encode(uint32_t cp, uint8_t *o)
{
    if (cp < 0x80) { o[0] = (uint8_t)cp; return 1; }
    if (cp < 0x800) { o[0] = (uint8_t)(0xC0 | (cp >> 6)); o[1] = (uint8_t)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { o[0] = (uint8_t)(0xE0 | (cp >> 12)); o[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (uint8_t)(0x80 | (cp & 0x3F)); return 3; }
    o[0] = (uint8_t)(0xF0 | (cp >> 18)); o[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (uint8_t)(0x80 | (cp & 0x3F));
    return 4;
}

TEST(core, locale_wc2mb_utf8_encoding)
{
    static const struct { const WCHAR *w; int nw; const char *utf8; int n; } v[] = {
        { u"A", 1, "A", 1 },
        { u"\x7F", 1, "\x7F", 1 },
        { u"\x80", 1, "\xC2\x80", 2 },
        { u"\u00E9", 1, "\xC3\xA9", 2 },
        { u"\u07FF", 1, "\xDF\xBF", 2 },
        { u"\u0800", 1, "\xE0\xA0\x80", 3 },
        { u"\u20AC", 1, "\xE2\x82\xAC", 3 },
        { u"\uD7FF", 1, "\xED\x9F\xBF", 3 },
        { u"\uE000", 1, "\xEE\x80\x80", 3 },
        { u"\uFEFF", 1, "\xEF\xBB\xBF", 3 },
        { u"\uFFFD", 1, "\xEF\xBF\xBD", 3 },
        { u"\uFFFF", 1, "\xEF\xBF\xBF", 3 },
        { u"\U00010000", 2, "\xF0\x90\x80\x80", 4 },
        { u"\U0001F600", 2, "\xF0\x9F\x98\x80", 4 },
        { u"\U0010FFFF", 2, "\xF4\x8F\xBF\xBF", 4 },
        { u"a\u00E9\u20AC\U0001F600z", 6, "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z", 11 },
    };
    char out[16];
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        fill_a(out, 16);
        OK_N(rt_wide_to_mb(CP_UTF8, 0, (const WCHAR *)v[i].w, v[i].nw, out, 16, NULL, NULL), v[i].n);
        T_TRUE(!memcmp(out, v[i].utf8, (size_t)v[i].n));
        T_TRUE(untouched_a(out + v[i].n, 16 - v[i].n));
        OK_N(rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, (const WCHAR *)v[i].w, v[i].nw, out, 16, NULL, NULL), v[i].n);
        OK_N(rt_wide_to_mb(CP_UTF8, 0, (const WCHAR *)v[i].w, v[i].nw, NULL, 0, NULL, NULL), v[i].n);
        /* One byte short fails, and no character is cut in half.  (A one-byte result has no
         * shorter buffer but zero, which is the size query.) */
        if (v[i].n > 1) {
            fill_a(out, 16);
            FAILS(rt_wide_to_mb(CP_UTF8, 0, (const WCHAR *)v[i].w, v[i].nw, out, v[i].n - 1, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
            T_TRUE(untouched_a(out + v[i].n - 1, 16 - (v[i].n - 1)));
        }
    }
    /* -1 includes the NUL. */
    OK_N(rt_wide_to_mb(CP_UTF8, 0, WSTR("\u00E9"), -1, out, 16, NULL, NULL), 3);
    T_EQ(out[2], 0);
    /* Whole characters only: "a" fits in 1 byte, the euro sign after it does not fit in 3. */
    fill_a(out, 16);
    FAILS(rt_wide_to_mb(CP_UTF8, 0, WSTR("a\u20AC"), 2, out, 3, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(out[0], 'a'); T_TRUE(untouched_a(out + 1, 15));
    fill_a(out, 16);
    FAILS(rt_wide_to_mb(CP_UTF8, 0, WSTR("\u00E9\u00E9"), 2, out, 3, NULL, NULL), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(!memcmp(out, "\xC3\xA9", 2)); T_TRUE(untouched_a(out + 2, 14));
}

TEST(core, locale_wc2mb_utf8_unpaired_surrogates)
{
    /* Windows (Vista and later) replaces an unpaired surrogate by U+FFFD, or fails with
     * WC_ERR_INVALID_CHARS. */
    static const struct { const WCHAR *w; int nw; const char *utf8; int n; } v[] = {
        { u"\xD800", 1, "\xEF\xBF\xBD", 3 },                          /* lone high */
        { u"\xDBFF", 1, "\xEF\xBF\xBD", 3 },
        { u"\xDC00", 1, "\xEF\xBF\xBD", 3 },                          /* lone low */
        { u"\xDFFF", 1, "\xEF\xBF\xBD", 3 },
        { u"\xDC00\xD800", 2, "\xEF\xBF\xBD\xEF\xBF\xBD", 6 },        /* reversed pair */
        { u"\xD800\xD800", 2, "\xEF\xBF\xBD\xEF\xBF\xBD", 6 },        /* two highs */
        { u"\xD800""a", 2, "\xEF\xBF\xBD" "a", 4 },                   /* high then not a low */
        { u"a\xDC00", 2, "a\xEF\xBF\xBD", 4 },
        { u"\xD800\xDC00\xDC00", 3, "\xF0\x90\x80\x80\xEF\xBF\xBD", 7 },   /* a pair, then a stray low */
        { u"\xD83D\xD83D\xDE00", 3, "\xEF\xBF\xBD\xF0\x9F\x98\x80", 7 },   /* a stray high, then a pair */
    };
    char out[16];
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        fill_a(out, 16);
        OK_N(rt_wide_to_mb(CP_UTF8, 0, (const WCHAR *)v[i].w, v[i].nw, out, 16, NULL, NULL), v[i].n);
        T_TRUE(!memcmp(out, v[i].utf8, (size_t)v[i].n));
        OK_N(rt_wide_to_mb(CP_UTF8, 0, (const WCHAR *)v[i].w, v[i].nw, NULL, 0, NULL, NULL), v[i].n);
        FAILS(rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, (const WCHAR *)v[i].w, v[i].nw, out, 16, NULL, NULL), ERROR_NO_UNICODE_TRANSLATION);
        FAILS(rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, (const WCHAR *)v[i].w, v[i].nw, NULL, 0, NULL, NULL), ERROR_NO_UNICODE_TRANSLATION);
    }
    /* A high surrogate that ends the explicit length is unpaired even if a low one follows in memory. */
    static const WCHAR pair[] = { 0xD83D, 0xDE00 };
    OK_N(rt_wide_to_mb(CP_UTF8, 0, pair, 1, out, 16, NULL, NULL), 3);
    T_TRUE(!memcmp(out, "\xEF\xBF\xBD", 3));
    FAILS(rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, pair, 1, out, 16, NULL, NULL), ERROR_NO_UNICODE_TRANSLATION);
    OK_N(rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, pair, 2, out, 16, NULL, NULL), 4);
}

TEST(core, locale_utf8_roundtrip_every_scalar_value)
{
    /* Every Unicode scalar value, 1,112,064 of them, encoded alone: the bytes are the textbook
     * encoding, the size query agrees, and MultiByteToWideChar with MB_ERR_INVALID_CHARS gives the
     * UTF-16 form back. */
    unsigned bad = 0;
    for (uint32_t cp = 0; cp <= 0x10FFFF && bad < 10; cp++) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        WCHAR w[2];
        int nw;
        if (cp >= 0x10000) { w[0] = (WCHAR)(0xD800 + ((cp - 0x10000) >> 10)); w[1] = (WCHAR)(0xDC00 + ((cp - 0x10000) & 0x3FF)); nw = 2; }
        else { w[0] = (WCHAR)cp; nw = 1; }
        uint8_t want[4];
        int nwant = ref_encode(cp, want);
        char out[4];
        int n = rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, w, nw, out, 4, NULL, NULL);
        int q = rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, w, nw, NULL, 0, NULL, NULL);
        WCHAR back[2] = { 0, 0 };
        int nb = rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, out, n, back, 2);
        if (n != nwant || q != nwant || memcmp(out, want, (size_t)nwant) || nb != nw || back[0] != w[0] || (nw == 2 && back[1] != w[1])) {
            bad++;
            rt_test_fail(__FILE__, __LINE__, "U+%04X: wrote %d bytes (query %d, expected %d), round trip %d units", cp, n, q, nwant, nb);
        }
    }
    T_EQ(bad, 0);
    /* The same, for all the BMP in one string: a long conversion crossing every length class. */
    static WCHAR big[0x10000 - 0x800];
    int nbig = 0;
    for (uint32_t cp = 0; cp < 0x10000; cp++) if (cp < 0xD800 || cp > 0xDFFF) big[nbig++] = (WCHAR)cp;
    static char enc[3 * 0x10000];
    static WCHAR dec[0x10000];
    int ne = rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, big, nbig, enc, (int)sizeof enc, NULL, NULL);
    T_TRUE(ne > nbig);
    int nd = rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, enc, ne, dec, 0x10000);
    T_EQ(nd, nbig);
    T_TRUE(!memcmp(big, dec, (size_t)nbig * 2));
}
