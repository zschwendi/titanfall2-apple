/* The locale core from several threads at once.
 *
 * Nothing in runtime/core/locale.c is mutable: the tables are constant data and every function
 * works on its arguments and the caller's buffers.  The core is therefore thread-safe by
 * construction, and this test is the check that the construction holds: eight host threads
 * run the same mix of conversions, classification, case mapping, comparison, locale and
 * date/time queries on shared read-only inputs and private outputs, thousands of times each,
 * and every result must equal the one the main thread computed first.
 *
 * Only calls that succeed are made from the threads.  A failing call sets the calling guest
 * thread's last error; these threads are plain host threads (the thread unit that creates guest
 * threads is not part of this unit), whose last error falls back to one process-wide variable in
 * runtime/core/thread.c, and a race on that variable would say nothing about the locale core.
 * The failure paths touch no shared state of this file either: they are covered, one thread at
 * a time, in the other locale tests.  The test harness is not thread-safe, so the threads only
 * count mismatches; the main thread asserts.
 */
#include <pthread.h>
#include <string.h>

#include "rt/locale.h"
#include "rt/test.h"

#define NTHREADS    8
#define ITERATIONS  3000

/* Shared, read-only inputs. */
static const char utf8_text[] = "Caf\xC3\xA9 \xE2\x82\xAC\xF0\x9F\x98\x80 \xCE\xB1\xCE\xB2\xD0\xB6 end";      /* 25 bytes with the NUL, 17 units */
static const char ansi_text[] = "Caf\xE9 \x80 \x99 \xFF end";                                               /* code page 1252 */
static const WCHAR wide_text[] = { 'M', 'i', 'x', 'E', 'd', ' ', 0xE9, 0xC9, 0x3B1, 0x391, 0x436, 0x416, '1', '2', '!', 0 };
static const WCHAR picture[] = { 'd', 'd', '/', 'M', 'M', '/', 'y', 'y', 'y', 'y', 0 };
static const WCHAR wide_upper[] = { 'M', 'I', 'X', 'E', 'D', ' ', 0xC9, 0xC9, 0x391, 0x391, 0x416, 0x416, '1', '2', '!', 0 };

typedef struct {
    int      id;
    unsigned mismatches;        /* results that differ from the main thread's */
    unsigned calls;
} Worker;

#define CHECK(cond)  do { w->calls++; if (!(cond)) w->mismatches++; } while (0)

static void *run(void *arg)
{
    Worker *w = arg;
    for (int it = 0; it < ITERATIONS; it++) {
        WCHAR wide[64];
        char narrow[96];
        WORD types[64];
        BOOL used;

        /* Conversions. */
        int n = rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_text, -1, wide, 64);
        CHECK(n == 17 && wide[0] == 'C' && wide[3] == 0xE9 && wide[5] == 0x20AC && wide[6] == 0xD83D && wide[7] == 0xDE00 && wide[11] == 0x436 && wide[16] == 0);
        int m = rt_wide_to_mb(CP_UTF8, WC_ERR_INVALID_CHARS, wide, n, narrow, 96, NULL, NULL);
        CHECK(m == (int)sizeof utf8_text && !memcmp(narrow, utf8_text, sizeof utf8_text));
        n = rt_mb_to_wide(1252, MB_ERR_INVALID_CHARS, ansi_text, -1, wide, 64);
        CHECK(n == 15 && wide[3] == 0xE9 && wide[5] == 0x20AC && wide[7] == 0x2122 && wide[9] == 0xFF);
        used = TRUE;
        m = rt_wide_to_mb(437, WC_NO_BEST_FIT_CHARS, wide, n, narrow, 96, NULL, &used);
        CHECK(m == 15 && used == TRUE && narrow[3] == (char)0x82 && narrow[5] == '?' && narrow[7] == '?' && narrow[9] == (char)0x98);
        used = TRUE;
        m = rt_wide_to_mb(1252, WC_NO_BEST_FIT_CHARS, wide, n, narrow, 96, NULL, &used);
        CHECK(m == 15 && used == FALSE && !memcmp(narrow, ansi_text, sizeof ansi_text));
        CHECK(rt_mb_to_wide(437, 0, "\x80\xE1", 2, wide, 64) == 2 && wide[0] == 0xC7 && wide[1] == 0xDF);
        CHECK(rt_mb_to_wide(CP_UTF8, 0, "\xF0\x9F\x98\x80", 4, NULL, 0) == 2);
        CHECK(rt_wide_to_mb(CP_UTF8, 0, wide_text, -1, NULL, 0, NULL, NULL) == 22);

        /* Classification, case, comparison. */
        /* CT_CTYPE1 carries C1_DEFINED on every defined character (Windows; see locale_text.c). */
        CHECK(rt_string_type(CT_CTYPE1, wide_text, -1, types) == TRUE && types[0] == (C1_UPPER | C1_ALPHA | C1_DEFINED) &&
              types[1] == (C1_LOWER | C1_ALPHA | C1_DEFINED) && types[5] == (C1_SPACE | C1_BLANK | C1_DEFINED) &&
              types[12] == (C1_DIGIT | C1_XDIGIT | C1_DEFINED) && types[14] == (C1_PUNCT | C1_DEFINED) && types[15] == (C1_CNTRL | C1_DEFINED));
        CHECK(rt_string_type(CT_CTYPE2, wide_text, 3, types) == TRUE && types[0] == C2_LEFTTORIGHT);
        CHECK(rt_string_type(CT_CTYPE3, wide_text, 3, types) == TRUE && types[0] == C3_ALPHA);
        CHECK(rt_lcmap_string(LOCALE_USER_DEFAULT, LCMAP_UPPERCASE, wide_text, -1, wide, 64) == 16 && !memcmp(wide, wide_upper, sizeof wide_upper));
        CHECK(rt_lcmap_string(0x409, LCMAP_LOWERCASE, wide_upper, 6, wide, 6) == 6 && wide[1] == 'i' && wide[5] == ' ');
        CHECK(rt_compare_string(0x409, NORM_IGNORECASE, wide_text, -1, wide_upper, -1) == CSTR_EQUAL);
        CHECK(rt_compare_string(0x409, 0, wide_text, -1, wide_upper, -1) == CSTR_GREATER_THAN);
        CHECK(rt_wchar_upper(0xFF) == 0x178 && rt_wchar_lower(0x416) == 0x436 && rt_wchar_case_known(0x1E00) == false && rt_wchar_type_known('a'));
        unsigned sum = 0;
        for (unsigned c = (unsigned)(it % 7); c < 0x530; c += 7) sum += rt_wchar_upper((WCHAR)c) ^ rt_wchar_lower((WCHAR)c);
        CHECK(sum != 0);

        /* Locale data, date and time. */
        CHECK(rt_locale_default() == 0x409 && rt_locale_is_valid(0x409, LCID_INSTALLED) == TRUE && rt_cp_acp() == 1252 && rt_cp_oemcp() == 437);
        CHECK(rt_locale_info(0x409, LOCALE_SLONGDATE, wide, 64) == 20 && wide[0] == 'd' && wide[18] == 'y');
        CHECK(rt_locale_info(0x409, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, wide, 2) == 2 && wide[0] == 1252 && wide[1] == 0);
        RtLocaleEnum e;
        CHECK(rt_locale_enum_begin(LCID_INSTALLED, &e) == TRUE && rt_locale_enum_next(&e, wide) == TRUE && wide[7] == '9' && rt_locale_enum_next(&e, wide) == FALSE);
        RtSystemTime d = { 2009, 2, 0, 13, 23, 31, 30, 0 };
        d.wMonth = (WORD)(1 + it % 12);
        d.wDay = (WORD)(1 + it % 28);
        CHECK(rt_date_format(0x409, DATE_LONGDATE, &d, NULL, wide, 64) > 20 && wide[0] != 0);
        CHECK(rt_date_format(0x409, 0, &d, picture, wide, 64) == 11 && wide[2] == '/' && wide[10] == 0);
        CHECK(rt_time_format(0x409, 0, &d, NULL, wide, 64) == 12 && wide[0] == '1' && wide[1] == '1' && wide[9] == 'P');
        RtSystemTime t;
        rt_systemtime_from_filetime(UINT64_C(128790414900000000), &t);
        CHECK(t.wYear == 2009 && t.wMonth == 2 && t.wDay == 13 && t.wHour == 23 && t.wDayOfWeek == 5);
    }
    return NULL;
}

TEST(core, locale_threads_agree)
{
    /* The main thread runs the loop once, as the reference: it must find no mismatch either. */
    Worker ref = { -1, 0, 0 };
    run(&ref);
    T_EQ(ref.mismatches, 0);
    T_TRUE(ref.calls >= 25 * ITERATIONS);

    pthread_t th[NTHREADS];
    Worker w[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) {
        w[i].id = i; w[i].mismatches = 0; w[i].calls = 0;
        T_EQ(pthread_create(&th[i], NULL, run, &w[i]), 0);
    }
    for (int i = 0; i < NTHREADS; i++) {
        T_EQ(pthread_join(th[i], NULL), 0);
        T_EQ(w[i].mismatches, 0);
        T_EQ(w[i].calls, ref.calls);
    }
}

/* The same function on the same input from many threads at once, byte for byte: the large tables
 * (every code unit through the case and type tables) give identical answers. */
typedef struct { uint64_t digest; } TableWorker;

static void *tables(void *arg)
{
    TableWorker *tw = arg;
    uint64_t h = 1469598103934665603ull;
    for (int round = 0; round < 4; round++) {
        for (unsigned c = 0; c < 0x10000; c++) {
            WCHAR u = (WCHAR)c;
            h = (h ^ rt_wchar_upper(u)) * 1099511628211ull;
            h = (h ^ rt_wchar_lower(u)) * 1099511628211ull;
            h = (h ^ (unsigned)rt_wchar_case_known(u)) * 1099511628211ull;
            h = (h ^ (unsigned)rt_wchar_type_known(u)) * 1099511628211ull;
            if (rt_wchar_type_known(u)) {
                WORD t[3];
                (void)rt_string_type(CT_CTYPE1, &u, 1, &t[0]);
                (void)rt_string_type(CT_CTYPE2, &u, 1, &t[1]);
                (void)rt_string_type(CT_CTYPE3, &u, 1, &t[2]);
                h = (h ^ t[0]) * 1099511628211ull;
                h = (h ^ t[1]) * 1099511628211ull;
                h = (h ^ t[2]) * 1099511628211ull;
            }
        }
    }
    tw->digest = h;
    return NULL;
}

TEST(core, locale_threads_tables_give_identical_answers)
{
    TableWorker ref;
    tables(&ref);
    pthread_t th[NTHREADS];
    TableWorker tw[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) T_EQ(pthread_create(&th[i], NULL, tables, &tw[i]), 0);
    for (int i = 0; i < NTHREADS; i++) {
        T_EQ(pthread_join(th[i], NULL), 0);
        T_TRUE(tw[i].digest == ref.digest);
    }
}

TEST(core, locale_init_ran)
{
    /* core/init.c has a weak no-op rt_locale_init; the real one replaces it at link time and runs in
     * rt_init, which every test goes through. */
    T_TRUE(rt_locale_initialized());
    rt_locale_init();                       /* idempotent */
    rt_locale_init();
    T_TRUE(rt_locale_initialized());
    T_EQ(rt_cp_acp(), 1252);
}
