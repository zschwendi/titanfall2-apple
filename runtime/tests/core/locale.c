/* Tests of the code page half of the locale core (runtime/core/locale.c, rt/locale.h):
 * GetACP, GetOEMCP, IsValidCodePage, GetCPInfo and MultiByteToWideChar.  WideCharToMultiByte
 * is in locale_wc2mb.c, classification and case in locale_text.c, locale data and date/time
 * in locale_info.c, the generated expectation tables in locale_tables.c.
 *
 * The oracles: Python's cp1252 / cp437 codecs (tables), Python's UTF-8 decoder (replacement
 * policy), the Microsoft documentation (flags, errors, lengths).  Where Windows differs from
 * the codec the test says so.  Core tests call the core directly, never a product shim.
 */
#include <limits.h>
#include <string.h>
#include <sys/mman.h>

#include "rt/locale.h"
#include "rt/machine.h"
#include "rt/test.h"

/* ---- expectation tables (locale_tables.c) ---- */
extern const uint16_t locale_exp_cp1252[256], locale_exp_cp437[256];
extern const uint8_t locale_exp_cp1252_undefined[5];
typedef struct { uint8_t nbytes, nunits, valid; uint8_t bytes[13]; uint16_t units[13]; } LocaleExpUtf8;
extern const LocaleExpUtf8 locale_exp_utf8[];
extern const unsigned locale_exp_utf8_n;

/* ---- helpers ---- */
#define SENTINEL 0x7A5A5A01u
static void set_err(void) { rt_set_last_error(T_CTX(), SENTINEL); }
/* A call that must fail: returns 0 and sets exactly this last error. */
#define FAILS(call, err)  do { set_err(); T_EQ((call), 0); T_EQ(T_LAST_ERROR(), (err)); } while (0)
/* A call that must succeed with this result and must leave last error alone. */
#define OK_N(call, n)     do { set_err(); T_EQ((call), (n)); T_EQ(T_LAST_ERROR(), SENTINEL); } while (0)

#define FILL 0xAAAAu
static void fill_w(WCHAR *p, int n) { for (int i = 0; i < n; i++) p[i] = FILL; }
static bool untouched_w(const WCHAR *p, int n) { for (int i = 0; i < n; i++) if (p[i] != FILL) return false; return true; }

/* ============================================================================================
 * Code page queries
 * ========================================================================================== */

TEST(core, locale_codepage_queries)
{
    /* GetACP / GetOEMCP: the machine is en-US, 1252 and 437, and the machine description agrees. */
    T_EQ(rt_cp_acp(), 1252);
    T_EQ(rt_cp_oemcp(), 437);
    T_EQ(rt_cp_acp(), rt_machine()->ansi_cp);
    T_EQ(rt_cp_oemcp(), rt_machine()->oem_cp);

    /* IsValidCodePage: the five code pages the core converts. */
    set_err();
    static const UINT good[] = { 1252, 437, 20127, 28591, 65001 };
    for (unsigned i = 0; i < 5; i++) T_EQ(rt_cp_is_valid(good[i]), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);                       /* a query never sets last error */

    /* Everything else is FALSE, including the special values (not code pages) and code pages
     * Windows has but this machine does not convert. */
    static const UINT bad[] = { 0, 1, 2, 3, 42, 65000, 850, 932, 936, 1250, 1251, 1200, 1201, 12000, 20127 + 1, 28592,
                                54936, 65002, 0x7FFFFFFF, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        set_err();
        T_EQ(rt_cp_is_valid(bad[i]), FALSE);
        T_EQ(T_LAST_ERROR(), SENTINEL);
    }
    /* The first refusal of a code page is logged once, with its number. */
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "IsValidCodePage(932): code page 932 is not supported"));
    unsigned before = T_LOG_COUNT(RT_CH_IMPORT);
    T_EQ(rt_cp_is_valid(932), FALSE);
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before);
    T_EQ(rt_cp_is_valid(437), TRUE);
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before);
}

TEST(core, locale_codepage_info)
{
    CPINFO ci;
    static const struct { UINT cp; UINT maxchar; } ok[] = {
        { 1252, 1 }, { 437, 1 }, { 20127, 1 }, { 28591, 1 }, { CP_UTF8, 4 },
        { CP_ACP, 1 }, { CP_OEMCP, 1 }, { CP_THREAD_ACP, 1 },
    };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        memset(&ci, 0x5C, sizeof ci);
        set_err();
        T_EQ(rt_cp_info(ok[i].cp, &ci), TRUE);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_EQ(ci.MaxCharSize, ok[i].maxchar);
        T_EQ(ci.DefaultChar[0], '?');
        T_EQ(ci.DefaultChar[1], 0);
        for (int k = 0; k < MAX_LEADBYTES; k++) T_EQ(ci.LeadByte[k], 0);   /* no DBCS lead bytes anywhere */
    }
    T_EQ(sizeof(CPINFO), 20);

    /* A code page we do not convert: FALSE, ERROR_INVALID_PARAMETER, one log line each call. */
    static const UINT bad[] = { CP_MACCP, CP_SYMBOL, CP_UTF7, 850, 932, 1250, 1200, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        unsigned before = T_LOG_COUNT(RT_CH_IMPORT);
        memset(&ci, 0x5C, sizeof ci);
        FAILS(rt_cp_info(bad[i], &ci), ERROR_INVALID_PARAMETER);
        T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before + 1);
        T_EQ(ci.MaxCharSize, 0x5C5C5C5Cu);                 /* untouched on failure */
    }
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "GetCPInfo: code page 932 is not supported"));
    /* NULL output. */
    FAILS(rt_cp_info(1252, NULL), ERROR_INVALID_PARAMETER);
    FAILS(rt_cp_info(932, NULL), ERROR_INVALID_PARAMETER);
}

/* ============================================================================================
 * MultiByteToWideChar: the single-byte code pages
 * ========================================================================================== */

TEST(core, locale_mb2wc_1252_every_byte)
{
    /* All 256 bytes, one call each and in one call, against Python's codec.  Windows differs
     * from the codec in exactly five bytes: 81 8D 8F 90 9D, which the codec refuses and Windows
     * maps to the control character of the same value. */
    uint8_t all[256];
    for (int i = 0; i < 256; i++) all[i] = (uint8_t)i;
    static const DWORD flag_sets[] = { 0, MB_PRECOMPOSED, MB_ERR_INVALID_CHARS, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS };
    for (unsigned f = 0; f < 4; f++) {
        for (int b = 0; b < 256; b++) {
            WCHAR w = FILL;
            OK_N(rt_mb_to_wide(1252, flag_sets[f], (const char *)&all[b], 1, &w, 1), 1);
            T_EQ(w, locale_exp_cp1252[b]);
        }
        WCHAR out[256];
        OK_N(rt_mb_to_wide(1252, flag_sets[f], (const char *)all, 256, out, 256), 256);
        for (int b = 0; b < 256; b++) T_EQ(out[b], locale_exp_cp1252[b]);
    }
    for (int i = 0; i < 5; i++) {
        uint8_t b = locale_exp_cp1252_undefined[i];
        WCHAR w;
        OK_N(rt_mb_to_wide(1252, MB_ERR_INVALID_CHARS, (const char *)&b, 1, &w, 1), 1);   /* valid even with the flag */
        T_EQ(w, b);
    }
    /* Spot checks that do not come from the table. */
    WCHAR w[4];
    OK_N(rt_mb_to_wide(1252, 0, "\x80\x99\xE9\xFF", 4, w, 4), 4);
    T_EQ(w[0], 0x20AC); T_EQ(w[1], 0x2122); T_EQ(w[2], 0x00E9); T_EQ(w[3], 0x00FF);
    /* The special values name the same code page. */
    OK_N(rt_mb_to_wide(CP_ACP, 0, "\x80", 1, w, 1), 1);
    T_EQ(w[0], 0x20AC);
    OK_N(rt_mb_to_wide(CP_THREAD_ACP, 0, "\x80", 1, w, 1), 1);
    T_EQ(w[0], 0x20AC);
}

TEST(core, locale_mb2wc_437_every_byte)
{
    uint8_t all[256];
    for (int i = 0; i < 256; i++) all[i] = (uint8_t)i;
    static const DWORD flag_sets[] = { 0, MB_PRECOMPOSED, MB_ERR_INVALID_CHARS, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS };
    for (unsigned f = 0; f < 4; f++) {
        WCHAR out[256];
        OK_N(rt_mb_to_wide(437, flag_sets[f], (const char *)all, 256, out, 256), 256);
        for (int b = 0; b < 256; b++) T_EQ(out[b], locale_exp_cp437[b]);
        for (int b = 0; b < 256; b++) {
            WCHAR w = FILL;
            OK_N(rt_mb_to_wide(437, flag_sets[f], (const char *)&all[b], 1, &w, 1), 1);
            T_EQ(w, locale_exp_cp437[b]);
        }
    }
    /* Without MB_USEGLYPHCHARS the controls stay controls. */
    WCHAR w[4];
    OK_N(rt_mb_to_wide(437, 0, "\x01\x1F\x7F\xE1", 4, w, 4), 4);
    T_EQ(w[0], 0x0001); T_EQ(w[1], 0x001F); T_EQ(w[2], 0x007F); T_EQ(w[3], 0x00DF);    /* 0xE1 is sharp s in Windows 437 */
    OK_N(rt_mb_to_wide(CP_OEMCP, 0, "\x80", 1, w, 1), 1);
    T_EQ(w[0], 0x00C7);
}

TEST(core, locale_mb2wc_latin1_and_ascii)
{
    uint8_t all[256];
    for (int i = 0; i < 256; i++) all[i] = (uint8_t)i;
    WCHAR out[256];
    OK_N(rt_mb_to_wide(28591, 0, (const char *)all, 256, out, 256), 256);
    for (int b = 0; b < 256; b++) T_EQ(out[b], b);
    OK_N(rt_mb_to_wide(28591, MB_ERR_INVALID_CHARS, (const char *)all, 256, out, 256), 256);

    OK_N(rt_mb_to_wide(20127, 0, (const char *)all, 128, out, 256), 128);
    for (int b = 0; b < 128; b++) T_EQ(out[b], b);
    OK_N(rt_mb_to_wide(20127, MB_ERR_INVALID_CHARS, (const char *)all, 128, out, 256), 128);

    /* US-ASCII has no character for bytes 0x80 and up.  With MB_ERR_INVALID_CHARS the call
     * fails, at the byte (so the part before it has been written). */
    for (int b = 0x80; b < 256; b++) {
        char s[3] = { 'a', (char)b, 'c' };
        fill_w(out, 8);
        FAILS(rt_mb_to_wide(20127, MB_ERR_INVALID_CHARS, s, 3, out, 8), ERROR_NO_UNICODE_TRANSLATION);
        FAILS(rt_mb_to_wide(20127, MB_ERR_INVALID_CHARS, s, 3, NULL, 0), ERROR_NO_UNICODE_TRANSLATION);   /* also in a size query */
        FAILS(rt_mb_to_wide(20127, MB_ERR_INVALID_CHARS | MB_PRECOMPOSED, s, 3, out, 8), ERROR_NO_UNICODE_TRANSLATION);
    }
    /* Without the flag Windows substitutes a character; which one was not verified, so the
     * core stops and names the byte. */
    T_TRAPS(rt_mb_to_wide(20127, 0, "\xE9", 1, out, 8), "byte 0xe9 in code page 20127");
    T_TRAPS(rt_mb_to_wide(20127, 0, "ab\x80", 3, NULL, 0), "byte 0x80 in code page 20127");
    T_TRAPS(rt_mb_to_wide(20127, MB_PRECOMPOSED, "\xFF", 1, out, 8), "MB_ERR_INVALID_CHARS");
}

/* ============================================================================================
 * MultiByteToWideChar: lengths, buffers, parameters
 * ========================================================================================== */

TEST(core, locale_mb2wc_lengths)
{
    WCHAR out[16];
    /* -1: the NUL is converted and counted. */
    fill_w(out, 16);
    OK_N(rt_mb_to_wide(1252, 0, "abc", -1, out, 16), 4);
    T_EQ(out[0], 'a'); T_EQ(out[1], 'b'); T_EQ(out[2], 'c'); T_EQ(out[3], 0); T_EQ(out[4], FILL);
    /* The empty string with -1 is just the NUL. */
    fill_w(out, 16);
    OK_N(rt_mb_to_wide(1252, 0, "", -1, out, 16), 1);
    T_EQ(out[0], 0); T_EQ(out[1], FILL);
    /* An explicit length converts exactly that many bytes and writes no NUL. */
    fill_w(out, 16);
    OK_N(rt_mb_to_wide(1252, 0, "abcdef", 3, out, 16), 3);
    T_EQ(out[2], 'c'); T_EQ(out[3], FILL);
    fill_w(out, 16);
    OK_N(rt_mb_to_wide(1252, 0, "abc", 4, out, 16), 4);          /* counting the NUL explicitly */
    T_EQ(out[3], 0);
    /* Embedded NULs are data. */
    fill_w(out, 16);
    OK_N(rt_mb_to_wide(1252, 0, "a\0b\0", 4, out, 16), 4);
    T_EQ(out[0], 'a'); T_EQ(out[1], 0); T_EQ(out[2], 'b'); T_EQ(out[3], 0);
    fill_w(out, 16);
    OK_N(rt_mb_to_wide(1252, 0, "a\0b", -1, out, 16), 2);       /* -1 stops at the first NUL */
    T_EQ(out[1], 0); T_EQ(out[2], FILL);
    /* Any negative length means NUL-terminated (as the NT source does; the documentation names -1). */
    fill_w(out, 16);
    OK_N(rt_mb_to_wide(1252, 0, "xy", -2, out, 16), 3);
    OK_N(rt_mb_to_wide(1252, 0, "xy", INT_MIN, out, 16), 3);
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "xy", -7, out, 16), 3);
    /* One byte. */
    OK_N(rt_mb_to_wide(1252, 0, "\xE9", 1, out, 16), 1);
    T_EQ(out[0], 0xE9);

    /* The size query: dst_len 0, the destination is not used (NULL, or anything). */
    OK_N(rt_mb_to_wide(1252, 0, "abc", -1, NULL, 0), 4);
    OK_N(rt_mb_to_wide(1252, 0, "abc", 3, NULL, 0), 3);
    OK_N(rt_mb_to_wide(1252, 0, "abc", 3, out, 0), 3);
    OK_N(rt_mb_to_wide(437, 0, "\x80\x81", 2, NULL, 0), 2);
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", 9, NULL, 0), 1 + 1 + 2);
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "\xC3\xA9", -1, NULL, 0), 2);

    /* Exact fit succeeds; one short fails and leaves the part that fitted. */
    fill_w(out, 16);
    OK_N(rt_mb_to_wide(1252, 0, "abcd", 4, out, 4), 4);
    T_EQ(out[3], 'd'); T_EQ(out[4], FILL);
    fill_w(out, 16);
    FAILS(rt_mb_to_wide(1252, 0, "abcd", 4, out, 3), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(out[0], 'a'); T_EQ(out[2], 'c'); T_EQ(out[3], FILL);   /* nothing beyond dst_len */
    fill_w(out, 16);
    FAILS(rt_mb_to_wide(1252, 0, "abcd", -1, out, 4), ERROR_INSUFFICIENT_BUFFER);   /* the NUL does not fit */
    T_EQ(out[3], 'd'); T_EQ(out[4], FILL);
    fill_w(out, 16);
    OK_N(rt_mb_to_wide(1252, 0, "abcd", -1, out, 5), 5);
    fill_w(out, 16);
    FAILS(rt_mb_to_wide(1252, 0, "ab", 2, out, 1), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(out[0], 'a'); T_EQ(out[1], FILL);
    fill_w(out, 16);
    FAILS(rt_mb_to_wide(437, 0, "abc", 3, out, 2), ERROR_INSUFFICIENT_BUFFER);
    FAILS(rt_mb_to_wide(28591, 0, "abc", 3, out, 2), ERROR_INSUFFICIENT_BUFFER);
    FAILS(rt_mb_to_wide(20127, 0, "abc", 3, out, 2), ERROR_INSUFFICIENT_BUFFER);
    /* An unsuccessful call after a successful one does not leave the old error behind: it sets its own. */
    OK_N(rt_mb_to_wide(1252, 0, "a", 1, out, 1), 1);
}

TEST(core, locale_mb2wc_maximum_lengths)
{
    /* A length of INT_MAX is a size, not a signal: with a destination too small the call
     * stops after the part that fits and never reads the rest of the claimed source. */
    long pg = 16384;
    char *map = mmap(NULL, 2 * (size_t)pg, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    T_REQUIRE(map != MAP_FAILED);
    T_EQ(mprotect(map + pg, (size_t)pg, PROT_NONE), 0);
    char *src = map + pg - 4;                      /* 4 readable bytes, then a guard page */
    memcpy(src, "abcd", 4);
    WCHAR out[8];
    fill_w(out, 8);
    FAILS(rt_mb_to_wide(1252, 0, src, INT_MAX, out, 4), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(out[3], 'd');
    FAILS(rt_mb_to_wide(437, 0, src, INT_MAX, out, 4), ERROR_INSUFFICIENT_BUFFER);
    FAILS(rt_mb_to_wide(CP_UTF8, 0, src, INT_MAX, out, 4), ERROR_INSUFFICIENT_BUFFER);
    FAILS(rt_mb_to_wide(1252, 0, src, INT_MAX, out, 1), ERROR_INSUFFICIENT_BUFFER);
    munmap(map, 2 * (size_t)pg);

    /* The size query of INT_MAX bytes on a page where every byte is a character: the answer is
     * INT_MAX, the largest count an int holds, and the bytes need not be read.  2 GiB of zero pages,
     * mapped read-only and never touched, costs only address space. */
    size_t big = (size_t)INT_MAX;
    char *z = mmap(NULL, big, PROT_READ, MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
    if (z != MAP_FAILED) {
        OK_N(rt_mb_to_wide(1252, 0, z, INT_MAX, NULL, 0), INT_MAX);
        OK_N(rt_mb_to_wide(437, MB_ERR_INVALID_CHARS, z, INT_MAX, NULL, 0), INT_MAX);
        OK_N(rt_mb_to_wide(28591, 0, z, INT_MAX, NULL, 0), INT_MAX);
        OK_N(rt_mb_to_wide(CP_ACP, 0, z, INT_MAX - 1, NULL, 0), INT_MAX - 1);
        munmap(z, big);
    }
}

TEST(core, locale_mb2wc_parameters)
{
    WCHAR out[8];
    fill_w(out, 8);
    static const UINT cps[] = { 1252, 437, 20127, 28591, CP_UTF8, CP_ACP, CP_OEMCP, CP_THREAD_ACP };
    for (unsigned i = 0; i < sizeof cps / sizeof cps[0]; i++) {
        UINT cp = cps[i];
        FAILS(rt_mb_to_wide(cp, 0, NULL, 3, out, 8), ERROR_INVALID_PARAMETER);        /* source NULL */
        FAILS(rt_mb_to_wide(cp, 0, NULL, -1, out, 8), ERROR_INVALID_PARAMETER);
        FAILS(rt_mb_to_wide(cp, 0, NULL, 3, NULL, 0), ERROR_INVALID_PARAMETER);
        FAILS(rt_mb_to_wide(cp, 0, "abc", 0, out, 8), ERROR_INVALID_PARAMETER);       /* source length 0 */
        FAILS(rt_mb_to_wide(cp, 0, "abc", 0, NULL, 0), ERROR_INVALID_PARAMETER);
        FAILS(rt_mb_to_wide(cp, 0, "abc", 3, out, -1), ERROR_INVALID_PARAMETER);      /* destination size negative */
        FAILS(rt_mb_to_wide(cp, 0, "abc", 3, out, INT_MIN), ERROR_INVALID_PARAMETER);
        FAILS(rt_mb_to_wide(cp, 0, "abc", 3, NULL, 1), ERROR_INVALID_PARAMETER);      /* NULL destination, nonzero size */
        FAILS(rt_mb_to_wide(cp, 0, "abc", 3, NULL, 8), ERROR_INVALID_PARAMETER);
        T_TRUE(untouched_w(out, 8));
    }
    /* The same pointer for both: the documented ERROR_INVALID_PARAMETER, whatever the sizes. */
    char same[16] = "hello";
    FAILS(rt_mb_to_wide(1252, 0, same, 6, (WCHAR *)(void *)same, 8), ERROR_INVALID_PARAMETER);
    FAILS(rt_mb_to_wide(CP_UTF8, 0, same, -1, (WCHAR *)(void *)same, 8), ERROR_INVALID_PARAMETER);
    FAILS(rt_mb_to_wide(1252, 0, same, 3, (WCHAR *)(void *)same, 1), ERROR_INVALID_PARAMETER);
    T_TRUE(!memcmp(same, "hello", 6));

    /* Order of the checks (NT source order, not verified for Windows 7): parameters, then the code
     * page, then flags.  A code page the runtime does not carry is refused before the flags are looked
     * at.  932 is a real Windows code page that we do not carry: Windows checks its flags first, so for
     * 932 this answer differs from Windows only when the flags are bad too (Wine kernel32/tests/codepage.c,
     * test_other_invalid_parameters). 0xdeadbeef is not a code page on Windows either, and gives the same
     * answer with bad flags or without. */
    FAILS(rt_mb_to_wide(932, 0x10, NULL, 3, out, 8), ERROR_INVALID_PARAMETER);        /* bad parameter beats bad flags and bad code page */
    FAILS(rt_mb_to_wide(932, 0x10, "abc", 3, out, 8), ERROR_INVALID_PARAMETER);      /* the code page beats bad flags (Windows: ERROR_INVALID_FLAGS) */
    FAILS(rt_mb_to_wide(932, 0, "abc", 3, out, 8), ERROR_INVALID_PARAMETER);
    FAILS(rt_mb_to_wide(0xdeadbeef, 0x10, "abc", 3, out, 8), ERROR_INVALID_PARAMETER);
    FAILS(rt_mb_to_wide(0xdeadbeef, 0, "abc", 3, out, 8), ERROR_INVALID_PARAMETER);
    FAILS(rt_mb_to_wide(1252, 0x10, "abc", 3, out, 8), ERROR_INVALID_FLAGS);         /* a page we carry: its flags are checked */
    /* A successful call touches nothing but the destination and leaves last error alone (OK_N). */
    OK_N(rt_mb_to_wide(1252, 0, "abc", 3, out, 8), 3);
}

TEST(core, locale_mb2wc_flags)
{
    WCHAR out[8];
    /* Single-byte code pages: MB_PRECOMPOSED (default), MB_ERR_INVALID_CHARS, both. */
    static const UINT sb[] = { 1252, 437, 20127, 28591, CP_ACP, CP_OEMCP, CP_THREAD_ACP };
    for (unsigned i = 0; i < sizeof sb / sizeof sb[0]; i++) {
        OK_N(rt_mb_to_wide(sb[i], 0, "abc", 3, out, 8), 3);
        OK_N(rt_mb_to_wide(sb[i], MB_PRECOMPOSED, "abc", 3, out, 8), 3);
        OK_N(rt_mb_to_wide(sb[i], MB_ERR_INVALID_CHARS, "abc", 3, out, 8), 3);
        OK_N(rt_mb_to_wide(sb[i], MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, "abc", 3, out, 8), 3);
        /* PRECOMPOSED and COMPOSITE exclude each other (documented). */
        FAILS(rt_mb_to_wide(sb[i], MB_PRECOMPOSED | MB_COMPOSITE, "abc", 3, out, 8), ERROR_INVALID_FLAGS);
        FAILS(rt_mb_to_wide(sb[i], MB_PRECOMPOSED | MB_COMPOSITE | MB_ERR_INVALID_CHARS, "abc", 3, out, 8), ERROR_INVALID_FLAGS);
        /* Bits that are no MB_ flag. */
        static const DWORD junk[] = { 0x10, 0x20, 0x40, 0x80, 0x100, 0x400, 0x1000, 0x80000000u, 0xFFFFFFF0u, 0xFFFFFFFFu, 0x18 };
        for (unsigned j = 0; j < sizeof junk / sizeof junk[0]; j++) {
            FAILS(rt_mb_to_wide(sb[i], junk[j], "abc", 3, out, 8), ERROR_INVALID_FLAGS);
            FAILS(rt_mb_to_wide(sb[i], junk[j] | MB_PRECOMPOSED, "abc", 3, out, 8), ERROR_INVALID_FLAGS);
        }
        /* Valid flags that the core does not implement: stop, naming the flag. */
        T_TRAPS(rt_mb_to_wide(sb[i], MB_COMPOSITE, "abc", 3, out, 8), "MB_COMPOSITE");
        T_TRAPS(rt_mb_to_wide(sb[i], MB_COMPOSITE | MB_ERR_INVALID_CHARS, "abc", 3, out, 8), "flags 0xa");
        T_TRAPS(rt_mb_to_wide(sb[i], MB_USEGLYPHCHARS, "abc", 3, out, 8), "MB_USEGLYPHCHARS");
        T_TRAPS(rt_mb_to_wide(sb[i], MB_USEGLYPHCHARS | MB_PRECOMPOSED, "abc", 3, NULL, 0), "MB_USEGLYPHCHARS");
    }
    /* UTF-8 accepts 0 and MB_ERR_INVALID_CHARS only. */
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "abc", 3, out, 8), 3);
    OK_N(rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, "abc", 3, out, 8), 3);
    static const DWORD u8bad[] = { MB_PRECOMPOSED, MB_COMPOSITE, MB_USEGLYPHCHARS, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS,
                                   MB_COMPOSITE | MB_ERR_INVALID_CHARS, MB_USEGLYPHCHARS | MB_ERR_INVALID_CHARS, 0x10,
                                   0x80000000u, 0xFFFFFFFFu, 0x7, 0xF };
    for (unsigned j = 0; j < sizeof u8bad / sizeof u8bad[0]; j++) FAILS(rt_mb_to_wide(CP_UTF8, u8bad[j], "abc", 3, out, 8), ERROR_INVALID_FLAGS);

    /* Unsupported code pages: the code page is checked before the flags, so it fails with
     * ERROR_INVALID_PARAMETER and one log line per call, bad flags or not. */
    static const UINT unsup[] = { CP_MACCP, CP_SYMBOL, CP_UTF7, 850, 932, 1250, 1200, 12000, 54936, 0xFFFFFFFFu, 28592, 1253 };
    for (unsigned i = 0; i < sizeof unsup / sizeof unsup[0]; i++) {
        unsigned before = T_LOG_COUNT(RT_CH_IMPORT);
        FAILS(rt_mb_to_wide(unsup[i], 0, "abc", 3, out, 8), ERROR_INVALID_PARAMETER);
        T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before + 1);
        FAILS(rt_mb_to_wide(unsup[i], MB_ERR_INVALID_CHARS, "abc", 3, out, 8), ERROR_INVALID_PARAMETER);
        T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before + 2);
        FAILS(rt_mb_to_wide(unsup[i], 0, "abc", 3, NULL, 0), ERROR_INVALID_PARAMETER);
        FAILS(rt_mb_to_wide(unsup[i], 0x10, "abc", 3, out, 8), ERROR_INVALID_PARAMETER);   /* code page first: one log line */
        T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before + 4);
    }
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "MultiByteToWideChar: code page 932 is not supported"));
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "code page 65000 is not supported"));
}

/* ============================================================================================
 * MultiByteToWideChar: UTF-8
 * ========================================================================================== */

TEST(core, locale_mb2wc_utf8_valid)
{
    static const struct { const char *utf8; int n; WCHAR w[4]; int nw; } v[] = {
        { "A", 1, { 'A' }, 1 },
        { "\x7F", 1, { 0x7F }, 1 },
        { "\xC2\x80", 2, { 0x80 }, 1 },                         /* shortest 2-byte */
        { "\xC3\xA9", 2, { 0xE9 }, 1 },
        { "\xDF\xBF", 2, { 0x7FF }, 1 },                        /* longest 2-byte */
        { "\xE0\xA0\x80", 3, { 0x800 }, 1 },                    /* shortest 3-byte */
        { "\xE2\x82\xAC", 3, { 0x20AC }, 1 },
        { "\xED\x9F\xBF", 3, { 0xD7FF }, 1 },                   /* last before the surrogates */
        { "\xEE\x80\x80", 3, { 0xE000 }, 1 },                   /* first after them */
        { "\xEF\xBB\xBF", 3, { 0xFEFF }, 1 },                   /* a byte-order mark is kept */
        { "\xEF\xBF\xBD", 3, { 0xFFFD }, 1 },
        { "\xEF\xBF\xBE", 3, { 0xFFFE }, 1 },                   /* noncharacters pass through */
        { "\xEF\xBF\xBF", 3, { 0xFFFF }, 1 },
        { "\xF0\x90\x80\x80", 4, { 0xD800, 0xDC00 }, 2 },       /* U+10000, shortest 4-byte */
        { "\xF0\x9F\x98\x80", 4, { 0xD83D, 0xDE00 }, 2 },       /* U+1F600 */
        { "\xF4\x8F\xBF\xBF", 4, { 0xDBFF, 0xDFFF }, 2 },       /* U+10FFFF, the last */
        { "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z", 11, { 'a', 0xE9, 0x20AC, 0xD83D }, 6 },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        WCHAR out[16];
        fill_w(out, 16);
        int nw = v[i].nw;
        OK_N(rt_mb_to_wide(CP_UTF8, 0, v[i].utf8, v[i].n, out, 16), nw);
        for (int k = 0; k < nw && k < 4; k++) T_EQ(out[k], v[i].w[k]);
        T_EQ(out[nw], FILL);
        OK_N(rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, v[i].utf8, v[i].n, out, 16), nw);
        OK_N(rt_mb_to_wide(CP_UTF8, 0, v[i].utf8, v[i].n, NULL, 0), nw);
    }
    /* The last case spelled out: it ends "...\xF0\x9F\x98\x80z". */
    WCHAR out[16];
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z", 11, out, 16), 6);
    T_EQ(out[4], 0xDE00); T_EQ(out[5], 'z');
    /* -1 includes the terminator; an embedded NUL ends the scan only with -1. */
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "\xC3\xA9", -1, out, 16), 2);
    T_EQ(out[1], 0);
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "a\0b", 3, out, 16), 3);
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "a\0b", -1, out, 16), 2);
}

TEST(core, locale_mb2wc_utf8_invalid_python_vectors)
{
    /* 877 sequences: every single byte; two-, three- and four-byte combinations of the lead and
     * continuation bytes around each boundary of Table 3-7 of the Unicode Standard; and the
     * example of Table 3-8.  Expected values come from Python's UTF-8 decoder with
     * errors='replace', which replaces each maximal invalid subpart by one U+FFFD.
     * Without MB_ERR_INVALID_CHARS the result must be that; with it the call fails exactly
     * when Python's strict decoder fails. */
    for (unsigned i = 0; i < locale_exp_utf8_n; i++) {
        const LocaleExpUtf8 *v = &locale_exp_utf8[i];
        WCHAR out[16];
        fill_w(out, 16);
        set_err();
        int n = rt_mb_to_wide(CP_UTF8, 0, (const char *)v->bytes, v->nbytes, out, 16);
        if (n != v->nunits || memcmp(out, v->units, v->nunits * 2) != 0 || out[v->nunits] != FILL) {
            rt_test_fail(__FILE__, __LINE__, "vector %u (%u bytes, first byte %#x): got %d units, expected %d", i, v->nbytes, v->bytes[0], n, v->nunits);
            continue;
        }
        T_EQ(T_LAST_ERROR(), SENTINEL);
        int n2 = rt_mb_to_wide(CP_UTF8, 0, (const char *)v->bytes, v->nbytes, NULL, 0);
        if (n2 != v->nunits) rt_test_fail(__FILE__, __LINE__, "vector %u: size query %d, expected %d", i, n2, v->nunits);
        set_err();
        n = rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)v->bytes, v->nbytes, out, 16);
        if (v->valid) {
            if (n != v->nunits) rt_test_fail(__FILE__, __LINE__, "vector %u: valid but failed with MB_ERR_INVALID_CHARS", i);
        } else {
            if (n != 0 || T_LAST_ERROR() != ERROR_NO_UNICODE_TRANSLATION)
                rt_test_fail(__FILE__, __LINE__, "vector %u (first byte %#x): invalid but returned %d, error %u", i, v->bytes[0], n, T_LAST_ERROR());
            set_err();
            n = rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)v->bytes, v->nbytes, NULL, 0);   /* also in a size query */
            if (n != 0 || T_LAST_ERROR() != ERROR_NO_UNICODE_TRANSLATION)
                rt_test_fail(__FILE__, __LINE__, "vector %u: invalid but the size query returned %d", i, n);
        }
    }
}

/* Table 3-7 of the Unicode Standard, written out as the list of acceptable byte ranges, for
 * the exhaustive property test below.  Returns the number of bytes of the one well-formed
 * character at the start of s[0..n), or 0 if there is none; *cp is its value. */
static int ref_first_char(const uint8_t *s, int n, uint32_t *cp)
{
    static const struct { uint8_t len, r[4][2]; } shapes[] = {
        { 1, { {0x00, 0x7F} } },
        { 2, { {0xC2, 0xDF}, {0x80, 0xBF} } },
        { 3, { {0xE0, 0xE0}, {0xA0, 0xBF}, {0x80, 0xBF} } },
        { 3, { {0xE1, 0xEC}, {0x80, 0xBF}, {0x80, 0xBF} } },
        { 3, { {0xED, 0xED}, {0x80, 0x9F}, {0x80, 0xBF} } },
        { 3, { {0xEE, 0xEF}, {0x80, 0xBF}, {0x80, 0xBF} } },
        { 4, { {0xF0, 0xF0}, {0x90, 0xBF}, {0x80, 0xBF}, {0x80, 0xBF} } },
        { 4, { {0xF1, 0xF3}, {0x80, 0xBF}, {0x80, 0xBF}, {0x80, 0xBF} } },
        { 4, { {0xF4, 0xF4}, {0x80, 0x8F}, {0x80, 0xBF}, {0x80, 0xBF} } },
    };
    for (unsigned k = 0; k < sizeof shapes / sizeof shapes[0]; k++) {
        int len = shapes[k].len;
        if (len > n) continue;
        bool match = true;
        for (int j = 0; j < len; j++) if (s[j] < shapes[k].r[j][0] || s[j] > shapes[k].r[j][1]) match = false;
        if (!match) continue;
        uint32_t c = len == 1 ? s[0] : len == 2 ? s[0] & 0x1Fu : len == 3 ? s[0] & 0x0Fu : s[0] & 0x07u;
        for (int j = 1; j < len; j++) c = (c << 6) | (s[j] & 0x3Fu);
        *cp = c;
        return len;
    }
    return 0;
}

/* The reference decode of a whole sequence: false if any part is not well-formed; otherwise
 * the UTF-16 code units in out[] (at most 3 for a 3-byte input). */
static bool ref_decode(const uint8_t *s, int n, WCHAR *out, int *nout)
{
    int u = 0;
    for (int i = 0; i < n;) {
        uint32_t cp;
        int len = ref_first_char(s + i, n - i, &cp);
        if (!len) return false;
        if (cp >= 0x10000) { out[u++] = (WCHAR)(0xD800 + ((cp - 0x10000) >> 10)); out[u++] = (WCHAR)(0xDC00 + ((cp - 0x10000) & 0x3FF)); }
        else out[u++] = (WCHAR)cp;
        i += len;
    }
    *nout = u;
    return true;
}

TEST(core, locale_mb2wc_utf8_exhaustive_validity)
{
    /* Every 1-, 2- and 3-byte sequence whose first byte is not ASCII (4.4 million calls): with
     * MB_ERR_INVALID_CHARS the call succeeds exactly when the whole sequence is well-formed
     * according to the reference (Table 3-7 as byte ranges), and then gives the reference
     * code units.  When it fails, the error is ERROR_NO_UNICODE_TRANSLATION. */
    uint8_t s[3];
    WCHAR out[8], ref[8];
    unsigned bad = 0;
    for (int b1 = 0; b1 < 256 && bad < 20; b1++) {
        for (int b2 = -1; b2 < 256 && bad < 20; b2++) {
            for (int b3 = -1; b3 < 256; b3++) {
                if ((b2 < 0) && (b3 >= 0)) continue;                 /* shorter sequences once */
                if (b1 < 0x80 && b2 >= 0 && b3 >= 0) continue;       /* ASCII first byte: covered by shorter loops */
                s[0] = (uint8_t)b1; s[1] = (uint8_t)b2; s[2] = (uint8_t)b3;
                int n = b2 < 0 ? 1 : b3 < 0 ? 2 : 3;
                int nref = 0;
                bool ok = ref_decode(s, n, ref, &nref);
                set_err();
                int got = rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)s, n, out, 8);
                bool good = ok ? (got == nref && !memcmp(out, ref, (size_t)nref * 2) && T_LAST_ERROR() == SENTINEL)
                               : (got == 0 && T_LAST_ERROR() == ERROR_NO_UNICODE_TRANSLATION);
                if (!good) {
                    bad++;
                    rt_test_fail(__FILE__, __LINE__, "%d bytes %02x %02x %02x: reference says %s, got %d (error %u)", n, b1,
                                 b2 < 0 ? 0 : b2, b3 < 0 ? 0 : b3, ok ? "valid" : "invalid", got, T_LAST_ERROR());
                    if (bad >= 20) break;
                }
                if (b3 < 0 && b2 >= 0) { /* the 2-byte prefix is done; the 3-byte loop continues */ }
            }
        }
    }
    T_EQ(bad, 0);
    /* Every 4-byte character boundary, exactly: the first and last code point of each shape, and the
     * first value outside it. */
    static const struct { uint8_t b[4]; bool ok; } edge[] = {
        { {0xF0, 0x90, 0x80, 0x80}, true }, { {0xF0, 0x8F, 0xBF, 0xBF}, false },
        { {0xF4, 0x8F, 0xBF, 0xBF}, true }, { {0xF4, 0x90, 0x80, 0x80}, false },
        { {0xF3, 0xBF, 0xBF, 0xBF}, true }, { {0xF5, 0x80, 0x80, 0x80}, false },
        { {0xF0, 0x90, 0x80, 0x7F}, false }, { {0xF0, 0x90, 0x80, 0xC0}, false },
    };
    for (unsigned i = 0; i < sizeof edge / sizeof edge[0]; i++) {
        set_err();
        int n = rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)edge[i].b, 4, out, 8);
        T_EQ(n != 0, edge[i].ok);
    }
}

TEST(core, locale_mb2wc_utf8_buffer_limits)
{
    WCHAR out[8];
    /* A surrogate pair is never split: with room for one unit the supplementary character fails
     * and nothing is written. */
    fill_w(out, 8);
    FAILS(rt_mb_to_wide(CP_UTF8, 0, "\xF0\x9F\x98\x80", 4, out, 1), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(untouched_w(out, 8));
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "\xF0\x9F\x98\x80", 4, out, 2), 2);
    T_EQ(out[0], 0xD83D); T_EQ(out[1], 0xDE00); T_EQ(out[2], FILL);
    fill_w(out, 8);
    FAILS(rt_mb_to_wide(CP_UTF8, 0, "a\xF0\x9F\x98\x80", 5, out, 2), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(out[0], 'a'); T_EQ(out[1], FILL);                     /* the part that fitted is there */
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "a\xF0\x9F\x98\x80", 5, out, 3), 3);
    fill_w(out, 8);
    FAILS(rt_mb_to_wide(CP_UTF8, 0, "\xC3\xA9\xC3\xA9", 4, out, 1), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(out[0], 0xE9); T_EQ(out[1], FILL);
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "\xC3\xA9\xC3\xA9", 4, out, 2), 2);
    /* An invalid sequence becomes one U+FFFD and needs one slot. */
    fill_w(out, 8);
    OK_N(rt_mb_to_wide(CP_UTF8, 0, "\xFF", 1, out, 1), 1);
    T_EQ(out[0], 0xFFFD);
    FAILS(rt_mb_to_wide(CP_UTF8, 0, "\xFF\xFF", 2, out, 1), ERROR_INSUFFICIENT_BUFFER);
    /* The error that comes first wins: invalid input before the end of the buffer. */
    FAILS(rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, "a\xFFzzz", 5, out, 2), ERROR_NO_UNICODE_TRANSLATION);
    FAILS(rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, "aazz\xFF", 5, out, 2), ERROR_INSUFFICIENT_BUFFER);
}

TEST(core, locale_mb2wc_overlap)
{
    /* Identical pointers are the documented ERROR_INVALID_PARAMETER (see locale_mb2wc_parameters).
     * Any other overlap of the used ranges is undefined in Windows and fatal here. */
    uint8_t buf[64];
    memset(buf, 'a', sizeof buf);
    WCHAR *wb = (WCHAR *)(void *)buf;
    T_TRAPS(rt_mb_to_wide(1252, 0, (const char *)buf + 2, 4, wb, 4), "buffers overlap");     /* dst starts before src, reaches into it */
    T_TRAPS(rt_mb_to_wide(1252, 0, (const char *)buf, 8, wb + 2, 8), "buffers overlap");     /* src starts before dst */
    buf[40] = 0;                                                                              /* a NUL for the -1 length */
    T_TRAPS(rt_mb_to_wide(CP_UTF8, 0, (const char *)buf + 3, -1, wb + 1, 8), "buffers overlap");
    T_TRAPS(rt_mb_to_wide(437, 0, (const char *)buf + 7, 1, wb, 4), "buffers overlap");       /* dst range is 8 bytes: [0,8) holds byte 7 */
    /* Adjacent but disjoint ranges are fine, in both directions. */
    memset(buf, 'a', sizeof buf);
    OK_N(rt_mb_to_wide(1252, 0, (const char *)buf + 8, 4, wb, 4), 4);            /* dst [0,8), src [8,12) */
    OK_N(rt_mb_to_wide(1252, 0, (const char *)buf, 8, wb + 4, 8), 8);            /* src [0,8), dst [8,24) */
    OK_N(rt_mb_to_wide(1252, 0, (const char *)buf + 16, 2, wb, 8), 2);           /* dst [0,16), src [16,18) */
    /* A zero-size destination is not used, so it cannot overlap anything. */
    OK_N(rt_mb_to_wide(1252, 0, (const char *)buf, 8, wb + 1, 0), 8);
}

TEST(core, locale_mb2wc_traps_name_the_value)
{
    WCHAR out[4];
    /* The fatal report names the Windows function, the case and the value; the guest caller is
     * there too. */
    T_TRAPS(rt_mb_to_wide(1252, MB_COMPOSITE, "a", 1, out, 4), "MultiByteToWideChar: flag MB_COMPOSITE (flags 0x2, code page 1252)");
    T_TRAPS(rt_mb_to_wide(437, MB_USEGLYPHCHARS, "a", 1, out, 4), "MultiByteToWideChar: flag MB_USEGLYPHCHARS (flags 0x4, code page 437)");
    T_TRAPS(rt_mb_to_wide(20127, 0, "\x9C", 1, out, 4), "MultiByteToWideChar: byte 0x9c in code page 20127");
    T_TRAPS(rt_mb_to_wide(1252, MB_COMPOSITE, "a", 1, out, 4), "FATAL unimplemented:");
}
