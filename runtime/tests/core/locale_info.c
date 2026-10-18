/* Tests of the locale data in runtime/core/locale.c: GetUserDefaultLCID, IsValidLocale,
 * EnumSystemLocalesW, GetLocaleInfoW, GetDateFormatW, GetTimeFormatW, and the FILETIME
 * conversion behind "the current time".  See locale.c in this directory for the conventions.
 *
 * The oracle for dates is Python's datetime (weekdays, FILETIME arithmetic, month and day
 * names).  The locale strings are the Windows 7 en-US values the core claims to hold; the test
 * is the specification of that table, entry by entry.
 */
#include <limits.h>
#include <string.h>

#include "rt/locale.h"
#include "rt/machine.h"
#include "rt/test.h"

typedef struct { uint64_t ft; uint16_t y, mo, dow, d, h, mi, s, ms; } LocaleExpFiletime;
extern const LocaleExpFiletime locale_exp_filetime[];
extern const unsigned locale_exp_filetime_n;
typedef struct { uint16_t y, m, d, dow; } LocaleExpWeekday;
extern const LocaleExpWeekday locale_exp_weekday[];
extern const unsigned locale_exp_weekday_n;

#define SENTINEL 0x7A5A5A01u
static void set_err(void) { rt_set_last_error(T_CTX(), SENTINEL); }
#define FAILS(call, err)  do { set_err(); T_EQ((call), 0); T_EQ(T_LAST_ERROR(), (err)); } while (0)
#define OK_N(call, n)     do { set_err(); T_EQ((call), (n)); T_EQ(T_LAST_ERROR(), SENTINEL); } while (0)

#define FILL 0xAAAAu
#define EN_US 0x0409u
#define W(lit)  ((const WCHAR *)u##lit)

static void fill_w(WCHAR *p, int n) { for (int i = 0; i < n; i++) p[i] = FILL; }
static bool untouched_w(const WCHAR *p, int n) { for (int i = 0; i < n; i++) if (p[i] != FILL) return false; return true; }

/* A wide buffer equals an ASCII string (and is NUL-terminated after it). */
static bool is_str(const WCHAR *w, const char *ascii)
{
    size_t n = strlen(ascii);
    for (size_t i = 0; i < n; i++) if (w[i] != (uint8_t)ascii[i]) return false;
    return w[n] == 0;
}

static RtSystemTime st(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s, unsigned ms)
{
    RtSystemTime t = { (WORD)y, (WORD)mo, 3, (WORD)d, (WORD)h, (WORD)mi, (WORD)s, (WORD)ms };   /* wDayOfWeek 3 is wrong on purpose: it is ignored */
    return t;
}

/* ============================================================================================
 * Locale identity
 * ========================================================================================== */

TEST(core, locale_default_and_validity)
{
    T_EQ(rt_locale_default(), 0x0409);
    T_EQ(rt_locale_default(), rt_machine()->lcid);
    static const LCID good[] = { EN_US, LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT };
    for (unsigned i = 0; i < 3; i++) {
        OK_N(rt_locale_is_valid(good[i], LCID_INSTALLED), TRUE);
        OK_N(rt_locale_is_valid(good[i], LCID_SUPPORTED), TRUE);
    }
    /* A flags value that is neither of the two: FALSE and ERROR_INVALID_FLAGS (from the NT source;
     * the documentation lists no error), without looking at the locale.  Flags 0 is not one of
     * them: it is a validity query, checked below. */
    static const DWORD badflags[] = { LCID_INSTALLED | LCID_SUPPORTED, LCID_ALTERNATE_SORTS, 8, 0x10, 0x100, 0x80000000u, 0xFFFFFFFFu, 5, 6 };
    for (unsigned i = 0; i < sizeof badflags / sizeof badflags[0]; i++) {
        FAILS(rt_locale_is_valid(EN_US, badflags[i]), ERROR_INVALID_FLAGS);
        FAILS(rt_locale_is_valid(0x0407, badflags[i]), ERROR_INVALID_FLAGS);
    }
    /* Flags 0 answers like a validity query, without ERROR_INVALID_FLAGS.  Wine
     * kernel32/tests/locale.c, test_LocaleNameToLCID (the block that runs when RtlLcidToLocaleName
     * exists): with flags 0, IsValidLocale is TRUE for 0x00010407 and LOCALE_INVARIANT, and FALSE
     * for LOCALE_NEUTRAL, LOCALE_SYSTEM_DEFAULT and LOCALE_USER_DEFAULT; the last error is not
     * asserted there, so the FALSE answers are not checked for it.  0x0409 is TRUE by inference
     * (the test asserts TRUE for two other valid locales).  Other locales with flags 0 stop below,
     * 0x00010407 included (TRUE in that test; not modelled, not in the brief). */
    OK_N(rt_locale_is_valid(EN_US, 0), TRUE);
    OK_N(rt_locale_is_valid(LOCALE_INVARIANT, 0), TRUE);
    T_EQ(rt_locale_is_valid(LOCALE_NEUTRAL, 0), FALSE);
    T_EQ(rt_locale_is_valid(LOCALE_USER_DEFAULT, 0), FALSE);
    T_EQ(rt_locale_is_valid(LOCALE_SYSTEM_DEFAULT, 0), FALSE);
    T_TRAPS(rt_locale_is_valid(0x0407, 0), "IsValidLocale: locale");
    /* The documented "do not use" value is not answered. */
    T_TRAPS(rt_locale_is_valid(EN_US, 0x39), "IsValidLocale: dwFlags 0x39");
    /* Any other locale with LCID_INSTALLED or LCID_SUPPORTED: the machine has one, and the honest
     * answer for the rest is not known (with flags 0, see above). */
    static const LCID others[] = { 0x0407, 0x0809, 0x0000, LOCALE_INVARIANT, 0x1000, 0x0C00, 0x1400, 0x10409, 0x0009, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++) {
        T_TRAPS(rt_locale_is_valid(others[i], LCID_INSTALLED), "IsValidLocale: locale");
        T_TRAPS(rt_locale_is_valid(others[i], LCID_SUPPORTED), "IsValidLocale: locale");
    }
    T_TRAPS(rt_locale_is_valid(0x0407, LCID_INSTALLED), "locale 0x407");
    T_TRAPS(rt_locale_is_valid(LOCALE_INVARIANT, LCID_SUPPORTED), "locale 0x7f");
}

TEST(core, locale_enumeration)
{
    RtLocaleEnum e;
    WCHAR name[RT_LOCALE_ENUM_NAME_MAX];
    /* 0 behaves as LCID_SUPPORTED; INSTALLED, SUPPORTED and their combinations with the alternate
     * sorts list the one locale, as eight upper-case hex digits. */
    static const DWORD with_locale[] = { 0, LCID_INSTALLED, LCID_SUPPORTED, LCID_INSTALLED | LCID_ALTERNATE_SORTS, LCID_SUPPORTED | LCID_ALTERNATE_SORTS };
    for (unsigned i = 0; i < sizeof with_locale / sizeof with_locale[0]; i++) {
        fill_w(name, RT_LOCALE_ENUM_NAME_MAX);
        OK_N(rt_locale_enum_begin(with_locale[i], &e), TRUE);
        OK_N(rt_locale_enum_next(&e, name), TRUE);
        T_TRUE(is_str(name, "00000409"));
        T_EQ(name[8], 0);
        fill_w(name, RT_LOCALE_ENUM_NAME_MAX);
        OK_N(rt_locale_enum_next(&e, name), FALSE);           /* the list ends, and stays ended */
        OK_N(rt_locale_enum_next(&e, name), FALSE);
        T_TRUE(untouched_w(name, RT_LOCALE_ENUM_NAME_MAX));
        /* A new enumeration starts again. */
        OK_N(rt_locale_enum_begin(with_locale[i], &e), TRUE);
        OK_N(rt_locale_enum_next(&e, name), TRUE);
    }
    /* Only alternate sorts: there are none. */
    OK_N(rt_locale_enum_begin(LCID_ALTERNATE_SORTS, &e), TRUE);
    fill_w(name, RT_LOCALE_ENUM_NAME_MAX);
    OK_N(rt_locale_enum_next(&e, name), FALSE);
    T_TRUE(untouched_w(name, RT_LOCALE_ENUM_NAME_MAX));
    /* INSTALLED and SUPPORTED exclude each other (documented); other bits are no flags. */
    static const DWORD bad[] = { LCID_INSTALLED | LCID_SUPPORTED, LCID_INSTALLED | LCID_SUPPORTED | LCID_ALTERNATE_SORTS, 8, 0x10, 0x100, 0x80000000u, 0xFFFFFFFFu, 9, 0x3F };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) FAILS(rt_locale_enum_begin(bad[i], &e), ERROR_INVALID_FLAGS);
    /* The name parses back to the locale with base 16, as the C runtime does. */
    OK_N(rt_locale_enum_begin(LCID_INSTALLED, &e), TRUE);
    T_TRUE(rt_locale_enum_next(&e, name));
    unsigned v = 0;
    for (int i = 0; name[i]; i++) v = v * 16 + (unsigned)(name[i] <= '9' ? name[i] - '0' : name[i] - 'A' + 10);
    T_EQ(v, rt_locale_default());
    /* Every enumerated locale is valid. */
    T_EQ(rt_locale_is_valid(v, LCID_INSTALLED), TRUE);
}

/* ============================================================================================
 * GetLocaleInfoW
 * ========================================================================================== */

typedef struct { LCTYPE type; const char *text; int32_t num; } InfoCase;     /* num < 0: no numeric form */

/* The table the core claims to hold, written out independently.  Entries marked [?] are the
 * least certain Windows 7 values (see locale.c). */
static const InfoCase info_cases[] = {
    { LOCALE_ILANGUAGE, "0409", 0x409 }, { LOCALE_SLANGUAGE, "English (United States)", -1 }, { LOCALE_SABBREVLANGNAME, "ENU", -1 },
    { LOCALE_SNATIVELANGNAME, "English", -1 }, { LOCALE_ICOUNTRY, "1", 1 }, { LOCALE_SCOUNTRY, "United States", -1 },
    { LOCALE_SABBREVCTRYNAME, "USA", -1 }, { LOCALE_SNATIVECTRYNAME, "United States", -1 }, { LOCALE_IDEFAULTLANGUAGE, "0409", 0x409 },
    { LOCALE_IDEFAULTCOUNTRY, "1", 1 }, { LOCALE_IDEFAULTCODEPAGE, "437", 437 }, { LOCALE_SLIST, ",", -1 }, { LOCALE_IMEASURE, "1", 1 },
    { LOCALE_SDECIMAL, ".", -1 }, { LOCALE_STHOUSAND, ",", -1 }, { LOCALE_SGROUPING, "3;0", -1 }, { LOCALE_IDIGITS, "2", 2 },
    { LOCALE_ILZERO, "1", 1 }, { LOCALE_SNATIVEDIGITS, "0123456789", -1 }, { LOCALE_SCURRENCY, "$", -1 }, { LOCALE_SINTLSYMBOL, "USD", -1 },
    { LOCALE_SMONDECIMALSEP, ".", -1 }, { LOCALE_SMONTHOUSANDSEP, ",", -1 }, { LOCALE_SMONGROUPING, "3;0", -1 }, { LOCALE_ICURRDIGITS, "2", 2 },
    { LOCALE_IINTLCURRDIGITS, "2", 2 }, { LOCALE_ICURRENCY, "0", 0 }, { LOCALE_INEGCURR, "0", 0 }, { LOCALE_SDATE, "/", -1 },
    { LOCALE_STIME, ":", -1 }, { LOCALE_SSHORTDATE, "M/d/yyyy", -1 }, { LOCALE_SLONGDATE, "dddd, MMMM dd, yyyy", -1 }, { LOCALE_IDATE, "0", 0 },
    { LOCALE_ILDATE, "0", 0 }, { LOCALE_ITIME, "0", 0 }, { LOCALE_ICENTURY, "1", 1 }, { LOCALE_ITLZERO, "0", 0 }, { LOCALE_IDAYLZERO, "0", 0 },
    { LOCALE_IMONLZERO, "0", 0 }, { LOCALE_S1159, "AM", -1 }, { LOCALE_S2359, "PM", -1 }, { LOCALE_SPOSITIVESIGN, "", -1 },
    { LOCALE_SNEGATIVESIGN, "-", -1 }, { LOCALE_IPOSSIGNPOSN, "3", 3 }, { LOCALE_INEGSIGNPOSN, "0", 0 }, { LOCALE_IPOSSYMPRECEDES, "1", 1 },
    { LOCALE_IPOSSEPBYSPACE, "0", 0 }, { LOCALE_INEGSYMPRECEDES, "1", 1 }, { LOCALE_INEGSEPBYSPACE, "0", 0 }, { LOCALE_SISO639LANGNAME, "en", -1 },
    { LOCALE_SISO3166CTRYNAME, "US", -1 }, { LOCALE_IGEOID, "244", 244 }, { LOCALE_SNAME, "en-US", -1 }, { LOCALE_SDURATION, "h:mm:ss", -1 },
    { LOCALE_SPARENT, "en", -1 }, { LOCALE_SENGLISHDISPLAYNAME, "English (United States)", -1 },
    { LOCALE_SNATIVEDISPLAYNAME, "English (United States)", -1 }, { LOCALE_SMONTHDAY, "MMMM dd", -1 }, { LOCALE_SSHORTTIME, "h:mm tt", -1 },
    { LOCALE_SENGLANGUAGE, "English", -1 }, { LOCALE_SENGCOUNTRY, "United States", -1 }, { LOCALE_STIMEFORMAT, "h:mm:ss tt", -1 },
    { LOCALE_IDEFAULTANSICODEPAGE, "1252", 1252 }, { LOCALE_SYEARMONTH, "MMMM, yyyy", -1 }, { LOCALE_ICALENDARTYPE, "1", 1 },
    { LOCALE_IPAPERSIZE, "1", 1 }, { LOCALE_IOPTIONALCALENDAR, "0", 0 }, { LOCALE_IFIRSTDAYOFWEEK, "6", 6 }, { LOCALE_IFIRSTWEEKOFYEAR, "0", 0 },
    { LOCALE_INEGNUMBER, "1", 1 }, { LOCALE_IDEFAULTMACCODEPAGE, "10000", 10000 }, { LOCALE_IDEFAULTEBCDICCODEPAGE, "37", 37 },
    { LOCALE_SSORTNAME, "Default", -1 },
};

static const char *const days[7] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
static const char *const days3[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
static const char *const months[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *const months3[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

TEST(core, locale_info_every_entry)
{
    WCHAR buf[64];
    static const LCID locales[] = { EN_US, LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT };
    for (unsigned c = 0; c < sizeof info_cases / sizeof info_cases[0]; c++) {
        const InfoCase *ic = &info_cases[c];
        int need = (int)strlen(ic->text) + 1;
        for (unsigned l = 0; l < 3; l++) {
            /* The size query: length with the NUL; the buffer is not used. */
            OK_N(rt_locale_info(locales[l], ic->type, NULL, 0), need);
            OK_N(rt_locale_info(locales[l], ic->type, buf, 0), need);
            /* The copy, with room to spare. */
            fill_w(buf, 64);
            OK_N(rt_locale_info(locales[l], ic->type, buf, 64), need);
            if (!is_str(buf, ic->text)) rt_test_fail(__FILE__, __LINE__, "LCTYPE %#x: wrong text", ic->type);
            T_EQ(buf[need], FILL);
        }
        /* Exact fit. */
        fill_w(buf, 64);
        OK_N(rt_locale_info(EN_US, ic->type, buf, need), need);
        T_TRUE(is_str(buf, ic->text));
        T_EQ(buf[need], FILL);
        /* One short: ERROR_INSUFFICIENT_BUFFER, and nothing is written. */
        if (need > 1) {
            fill_w(buf, 64);
            FAILS(rt_locale_info(EN_US, ic->type, buf, need - 1), ERROR_INSUFFICIENT_BUFFER);
            T_TRUE(untouched_w(buf, 64));
        }
        /* The two flags that change nothing in the wide function. */
        fill_w(buf, 64);
        OK_N(rt_locale_info(EN_US, ic->type | LOCALE_NOUSEROVERRIDE, buf, 64), need);
        T_TRUE(is_str(buf, ic->text));
        OK_N(rt_locale_info(EN_US, ic->type | LOCALE_USE_CP_ACP, buf, 64), need);
        OK_N(rt_locale_info(EN_US, ic->type | LOCALE_USE_CP_ACP | LOCALE_NOUSEROVERRIDE, buf, 64), need);
        T_TRUE(is_str(buf, ic->text));
        /* The number: the types that have one return it as a DWORD in two WCHARs. */
        if (ic->num >= 0) {
            fill_w(buf, 64);
            OK_N(rt_locale_info(EN_US, ic->type | LOCALE_RETURN_NUMBER, buf, 2), 2);
            uint32_t v;
            memcpy(&v, buf, 4);
            T_EQ(v, ic->num);
            T_EQ(buf[2], FILL);
            OK_N(rt_locale_info(EN_US, ic->type | LOCALE_RETURN_NUMBER, buf, 64), 2);
            OK_N(rt_locale_info(EN_US, ic->type | LOCALE_RETURN_NUMBER, NULL, 0), 2);
            OK_N(rt_locale_info(EN_US, ic->type | LOCALE_RETURN_NUMBER | LOCALE_NOUSEROVERRIDE, buf, 2), 2);
            fill_w(buf, 64);
            FAILS(rt_locale_info(EN_US, ic->type | LOCALE_RETURN_NUMBER, buf, 1), ERROR_INSUFFICIENT_BUFFER);
            T_TRUE(untouched_w(buf, 64));
        } else {
            T_TRAPS(rt_locale_info(EN_US, ic->type | LOCALE_RETURN_NUMBER, buf, 2), "LOCALE_RETURN_NUMBER with LCTYPE");
        }
    }
}

TEST(core, locale_info_names_of_days_and_months)
{
    WCHAR buf[32];
    for (unsigned i = 0; i < 7; i++) {
        OK_N(rt_locale_info(EN_US, LOCALE_SDAYNAME1 + i, buf, 32), (int)strlen(days[i]) + 1);
        T_TRUE(is_str(buf, days[i]));
        OK_N(rt_locale_info(EN_US, LOCALE_SABBREVDAYNAME1 + i, buf, 32), 4);
        T_TRUE(is_str(buf, days3[i]));
    }
    for (unsigned i = 0; i < 12; i++) {
        OK_N(rt_locale_info(EN_US, LOCALE_SMONTHNAME1 + i, buf, 32), (int)strlen(months[i]) + 1);
        T_TRUE(is_str(buf, months[i]));
        OK_N(rt_locale_info(EN_US, LOCALE_SABBREVMONTHNAME1 + i, buf, 32), 4);
        T_TRUE(is_str(buf, months3[i]));
    }
    T_EQ(LOCALE_SDAYNAME7, LOCALE_SDAYNAME1 + 6);
    T_EQ(LOCALE_SMONTHNAME12, LOCALE_SMONTHNAME1 + 11);
    T_EQ(LOCALE_SABBREVDAYNAME7, LOCALE_SABBREVDAYNAME1 + 6);
    T_EQ(LOCALE_SABBREVMONTHNAME12, LOCALE_SABBREVMONTHNAME1 + 11);
    /* The names have no numeric form. */
    T_TRAPS(rt_locale_info(EN_US, LOCALE_SDAYNAME1 | LOCALE_RETURN_NUMBER, buf, 2), "no numeric form");
    T_TRAPS(rt_locale_info(EN_US, LOCALE_SMONTHNAME12 | LOCALE_RETURN_NUMBER, buf, 2), "no numeric form");
}

TEST(core, locale_info_agrees_with_the_machine)
{
    /* The values the rest of the runtime states elsewhere must be the ones in the table. */
    WCHAR buf[16];
    uint32_t v;
    OK_N(rt_locale_info(EN_US, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, buf, 2), 2);
    memcpy(&v, buf, 4);
    T_EQ(v, rt_cp_acp());
    T_EQ(v, rt_machine()->ansi_cp);
    OK_N(rt_locale_info(EN_US, LOCALE_IDEFAULTCODEPAGE | LOCALE_RETURN_NUMBER, buf, 2), 2);
    memcpy(&v, buf, 4);
    T_EQ(v, rt_cp_oemcp());
    T_EQ(v, rt_machine()->oem_cp);
    OK_N(rt_locale_info(EN_US, LOCALE_ILANGUAGE | LOCALE_RETURN_NUMBER, buf, 2), 2);
    memcpy(&v, buf, 4);
    T_EQ(v, rt_locale_default());
    /* The ANSI code page string converts as code page 1252 does. */
    OK_N(rt_locale_info(EN_US, LOCALE_IDEFAULTANSICODEPAGE, buf, 16), 5);
    T_TRUE(is_str(buf, "1252"));
    T_EQ(rt_cp_is_valid(1252), TRUE);
    T_EQ(rt_cp_is_valid(437), TRUE);
}

TEST(core, locale_info_errors_and_refusals)
{
    WCHAR buf[8];
    /* Parameters first (before the locale or the type). */
    FAILS(rt_locale_info(EN_US, LOCALE_SNAME, buf, -1), ERROR_INVALID_PARAMETER);
    FAILS(rt_locale_info(EN_US, LOCALE_SNAME, buf, INT_MIN), ERROR_INVALID_PARAMETER);
    FAILS(rt_locale_info(EN_US, LOCALE_SNAME, NULL, 1), ERROR_INVALID_PARAMETER);
    FAILS(rt_locale_info(EN_US, LOCALE_SNAME, NULL, 100), ERROR_INVALID_PARAMETER);
    FAILS(rt_locale_info(0x407, 0x12345, NULL, 1), ERROR_INVALID_PARAMETER);
    /* Locales other than en-US. */
    static const LCID others[] = { 0x0407, LOCALE_INVARIANT, 0x0809, 0x1000, 0x0C00, 0x1400, 0x10409, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++) T_TRAPS(rt_locale_info(others[i], LOCALE_SNAME, buf, 8), "GetLocaleInfoW: locale");
    /* Locale 0 is the user default. */
    OK_N(rt_locale_info(LOCALE_NEUTRAL, LOCALE_SNAME, buf, 8), 6);
    T_TRUE(!memcmp(buf, W("en-US"), 6 * sizeof(WCHAR)));
    T_TRAPS(rt_locale_info(0x0407, LOCALE_SNAME, buf, 8), "locale 0x407");
    T_TRAPS(rt_locale_info(LOCALE_INVARIANT, LOCALE_SNAME, NULL, 0), "locale 0x7f");
    /* An LCTYPE the table does not have. */
    static const LCTYPE missing[] = { 0, LOCALE_ILANGUAGE - 1, 0x58 /* LOCALE_FONTSIGNATURE */, 0x100E /* LOCALE_SMONTHNAME13 */,
                                      0x100F, 0x60 /* LOCALE_SSHORTESTDAYNAME1 */, 0x1007 /* LOCALE_SENGCURRNAME */, 0x1008, 0x1014, 0x7B, 0xFFFF, 0x5E };
    for (unsigned i = 0; i < sizeof missing / sizeof missing[0]; i++) T_TRAPS(rt_locale_info(EN_US, missing[i], buf, 8), "is not in the en-US table");
    T_TRAPS(rt_locale_info(EN_US, 0x58, buf, 8), "LCTYPE 0x58 (argument 0x58)");
    T_TRAPS(rt_locale_info(EN_US, 0x58 | LOCALE_NOUSEROVERRIDE, buf, 8), "LCTYPE 0x58 (argument 0x80000058)");
    T_TRAPS(rt_locale_info(EN_US, 0x01000000u | LOCALE_SNAME, buf, 8), "is not in the en-US table");       /* a stray bit is not a flag */
    T_TRAPS(rt_locale_info(EN_US, 0x08000000u | LOCALE_SNAME, buf, 8), "is not in the en-US table");           /* LOCALE_ALLOW_NEUTRAL_NAMES is for the Ex function */
    /* Genitive month names. */
    T_TRAPS(rt_locale_info(EN_US, LOCALE_SMONTHNAME1 | LOCALE_RETURN_GENITIVE_NAMES, buf, 8), "LOCALE_RETURN_GENITIVE_NAMES");
}

/* ============================================================================================
 * FILETIME to SYSTEMTIME
 * ========================================================================================== */

TEST(core, locale_systemtime_from_filetime)
{
    for (unsigned i = 0; i < locale_exp_filetime_n; i++) {
        const LocaleExpFiletime *e = &locale_exp_filetime[i];
        RtSystemTime t;
        memset(&t, 0xEE, sizeof t);
        rt_systemtime_from_filetime(e->ft, &t);
        if (t.wYear != e->y || t.wMonth != e->mo || t.wDayOfWeek != e->dow || t.wDay != e->d || t.wHour != e->h || t.wMinute != e->mi ||
            t.wSecond != e->s || t.wMilliseconds != e->ms)
            rt_test_fail(__FILE__, __LINE__, "FILETIME %llu: got %u-%02u-%02u dow %u %02u:%02u:%02u.%03u, expected %u-%02u-%02u dow %u %02u:%02u:%02u.%03u",
                         (unsigned long long)e->ft, t.wYear, t.wMonth, t.wDay, t.wDayOfWeek, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                         e->y, e->mo, e->d, e->dow, e->h, e->mi, e->s, e->ms);
    }
    /* Sub-millisecond units are cut, not rounded. */
    RtSystemTime t;
    rt_systemtime_from_filetime(9999, &t);
    T_EQ(t.wMilliseconds, 0);
    rt_systemtime_from_filetime(10000, &t);
    T_EQ(t.wMilliseconds, 1);
    rt_systemtime_from_filetime(10000 * 86400000ull - 1, &t);                 /* the last unit of 1601-01-01 */
    T_EQ(t.wDay, 1); T_EQ(t.wHour, 23); T_EQ(t.wMinute, 59); T_EQ(t.wSecond, 59); T_EQ(t.wMilliseconds, 999);
    rt_systemtime_from_filetime(10000 * 86400000ull, &t);
    T_EQ(t.wDay, 2); T_EQ(t.wHour, 0);
    /* One day at a time across 400 years (a whole Gregorian cycle), consecutive days advance the weekday by one. */
    unsigned prev_dow = 99, prev_d = 0, prev_m = 0, prev_y = 0;
    for (uint64_t day = 0; day < 146097; day++) {
        rt_systemtime_from_filetime(day * 864000000000ull, &t);
        if (prev_dow != 99) {
            T_EQ(t.wDayOfWeek, (prev_dow + 1) % 7);
            bool same_month = t.wMonth == prev_m && t.wYear == prev_y;
            if (same_month) T_EQ(t.wDay, prev_d + 1); else T_EQ(t.wDay, 1);
        }
        prev_dow = t.wDayOfWeek; prev_d = t.wDay; prev_m = t.wMonth; prev_y = t.wYear;
    }
    T_EQ(prev_y, 1601 + 399); T_EQ(prev_m, 12); T_EQ(prev_d, 31);               /* 400 years later, the last day of the cycle */
    /* The largest year SYSTEMTIME holds. */
    rt_systemtime_from_filetime(UINT64_C(0x7FFFFFFFFFFFFFFF), &t);
    T_EQ(t.wYear, 30828);                                                         /* 2^63 units is early in 30828; documented limit of FileTimeToSystemTime is 30827 */
}

/* ============================================================================================
 * GetDateFormatW
 * ========================================================================================== */

#define DATE(out, flags, tm, fmt) rt_date_format(EN_US, (flags), (tm), (fmt), (out), 64)

TEST(core, locale_date_default_formats)
{
    WCHAR buf[64];
    static const struct { unsigned y, m, d; const char *shortd, *longd, *ym; } v[] = {
        { 2009, 2, 13, "2/13/2009", "Friday, February 13, 2009", "February, 2009" },
        { 2024, 2, 29, "2/29/2024", "Thursday, February 29, 2024", "February, 2024" },
        { 1601, 1, 1, "1/1/1601", "Monday, January 01, 1601", "January, 1601" },
        { 9999, 12, 31, "12/31/9999", "Friday, December 31, 9999", "December, 9999" },
        { 2000, 1, 1, "1/1/2000", "Saturday, January 01, 2000", "January, 2000" },
        { 1994, 8, 31, "8/31/1994", "Wednesday, August 31, 1994", "August, 1994" },
        { 2026, 10, 8, "10/8/2026", "Thursday, October 08, 2026", "October, 2026" },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        RtSystemTime t = st(v[i].y, v[i].m, v[i].d, 0, 0, 0, 0);
        static const LCID locales[] = { EN_US, LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT };
        for (unsigned l = 0; l < 3; l++) {
            LCID loc = locales[l];
            /* No flags: the short date is the default. */
            fill_w(buf, 64);
            OK_N(rt_date_format(loc, 0, &t, NULL, buf, 64), (int)strlen(v[i].shortd) + 1);
            T_TRUE(is_str(buf, v[i].shortd));
            OK_N(rt_date_format(loc, DATE_SHORTDATE, &t, NULL, buf, 64), (int)strlen(v[i].shortd) + 1);
            T_TRUE(is_str(buf, v[i].shortd));
            OK_N(rt_date_format(loc, DATE_LONGDATE, &t, NULL, buf, 64), (int)strlen(v[i].longd) + 1);
            T_TRUE(is_str(buf, v[i].longd));
            OK_N(rt_date_format(loc, DATE_YEARMONTH, &t, NULL, buf, 64), (int)strlen(v[i].ym) + 1);
            T_TRUE(is_str(buf, v[i].ym));
            /* The two LOCALE_ flags are accepted and change nothing. */
            OK_N(rt_date_format(loc, LOCALE_NOUSEROVERRIDE | DATE_LONGDATE, &t, NULL, buf, 64), (int)strlen(v[i].longd) + 1);
            T_TRUE(is_str(buf, v[i].longd));
            OK_N(rt_date_format(loc, LOCALE_USE_CP_ACP, &t, NULL, buf, 64), (int)strlen(v[i].shortd) + 1);
            OK_N(rt_date_format(loc, LOCALE_USE_CP_ACP | LOCALE_NOUSEROVERRIDE | DATE_YEARMONTH, &t, NULL, buf, 64), (int)strlen(v[i].ym) + 1);
        }
    }
}

TEST(core, locale_date_weekdays_and_names)
{
    WCHAR buf[64];
    /* The weekday is computed from the date; wDayOfWeek in the input is ignored. */
    for (unsigned i = 0; i < locale_exp_weekday_n; i++) {
        const LocaleExpWeekday *w = &locale_exp_weekday[i];
        for (unsigned given = 0; given < 8; given += 3) {
            RtSystemTime t = st(w->y, w->m, w->d, 0, 0, 0, 0);
            t.wDayOfWeek = (WORD)given;
            OK_N(rt_date_format(EN_US, 0, &t, W("dddd"), buf, 64), (int)strlen(days[(w->dow + 6) % 7]) + 1);
            if (!is_str(buf, days[(w->dow + 6) % 7])) rt_test_fail(__FILE__, __LINE__, "%u-%u-%u: weekday %u", w->y, w->m, w->d, w->dow);
            OK_N(rt_date_format(EN_US, 0, &t, W("ddd"), buf, 64), 4);
            T_TRUE(is_str(buf, days3[(w->dow + 6) % 7]));
        }
    }
    /* Month names, all twelve. */
    for (unsigned m = 1; m <= 12; m++) {
        RtSystemTime t = st(2001, m, 1, 0, 0, 0, 0);
        OK_N(rt_date_format(EN_US, 0, &t, W("MMMM"), buf, 64), (int)strlen(months[m - 1]) + 1);
        T_TRUE(is_str(buf, months[m - 1]));
        OK_N(rt_date_format(EN_US, 0, &t, W("MMM"), buf, 64), 4);
        T_TRUE(is_str(buf, months3[m - 1]));
        OK_N(rt_date_format(EN_US, 0, &t, W("M"), buf, 64), m < 10 ? 2 : 3);
        OK_N(rt_date_format(EN_US, 0, &t, W("MM"), buf, 64), 3);
        T_EQ(buf[0], '0' + m / 10); T_EQ(buf[1], '0' + m % 10);
    }
}

TEST(core, locale_date_pictures)
{
    WCHAR buf[128];
    RtSystemTime may = st(1993, 5, 7, 0, 0, 0, 0);
    RtSystemTime aug = st(1994, 8, 31, 0, 0, 0, 0);
    static const struct { const WCHAR *pic; int t; const char *want; } v[] = {
        { u"d", 0, "7" }, { u"dd", 0, "07" }, { u"ddd", 0, "Fri" }, { u"dddd", 0, "Friday" },
        { u"d", 1, "31" }, { u"dd", 1, "31" },
        { u"M", 0, "5" }, { u"MM", 0, "05" }, { u"MMM", 0, "May" }, { u"MMMM", 0, "May" },
        { u"yy", 0, "93" }, { u"yyyy", 0, "1993" },
        /* Longer runs than the picture letters allow are cut to the longest: dddd, MMMM, yyyy; yyy is yyyy */
        { u"ddddd", 0, "Friday" }, { u"dddddddd", 0, "Friday" }, { u"MMMMM", 1, "August" }, { u"yyy", 0, "1993" },
        { u"yyyyy", 0, "1993" }, { u"yyyyyyy", 0, "1993" },
        { u"dd/MM/yyyy", 0, "07/05/1993" }, { u"yyyy-MM-dd", 1, "1994-08-31" }, { u"dddd, d MMMM yy", 1, "Wednesday, 31 August 94" },
        /* The documentation's examples. */
        { u"ddd',' MMM dd yy", 1, "Wed, Aug 31 94" }, { u"MMMM ''''yy", 0, "May '93" },
        /* Quoting: text is copied, a doubled quote is one quote, an unterminated quote runs to the end. */
        { u"'abc''bar'", 0, "abc'bar" }, { u"'dd' dd", 0, "dd 07" }, { u"'yyyy'", 0, "yyyy" }, { u"d 'of' MMMM", 0, "7 of May" },
        { u"'unterminated dd", 0, "unterminated dd" }, { u"dd'", 0, "07" }, { u"''", 0, "" }, { u"'''", 0, "'" },
        { u"''''", 0, "'" }, { u"a''b", 0, "ab" }, { u"d''d", 0, "77" }, { u"'a' 'b'", 0, "a b" },
        /* Characters that are no picture letter are copied, unquoted: spaces, punctuation, other letters, and the time letters. */
        { u"x d, y.. ", 0, NULL }, { u"hh:mm:ss tt", 0, "hh:mm:ss tt" }, { u"D m s H", 0, "D m s H" },
        { u"/ - . , : ; ( )", 0, "/ - . , : ; ( )" }, { u"MM dd", 0, "05 07" },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        if (!v[i].want) continue;                               /* "y" alone is refused; see the trap tests */
        fill_w(buf, 128);
        int want = (int)strlen(v[i].want) + 1;
        set_err();
        int got = rt_date_format(EN_US, 0, (v[i].t ? &aug : &may), v[i].pic, buf, 128);
        if (got != want || !is_str(buf, v[i].want))
            rt_test_fail(__FILE__, __LINE__, "picture #%u: got %d chars, expected \"%s\" (%d)", i, got, v[i].want, want);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        OK_N(rt_date_format(EN_US, 0, (v[i].t ? &aug : &may), v[i].pic, NULL, 0), want);              /* the size query agrees */
        /* LOCALE_NOUSEROVERRIDE with a picture is ERROR_INVALID_FLAGS; buffer untouched.  Windows:
         * Wine test_GetDateFormatA, "Same as above but with LOCALE_NOUSEROVERRIDE". */
        fill_w(buf, 128);
        FAILS(rt_date_format(EN_US, LOCALE_NOUSEROVERRIDE, (v[i].t ? &aug : &may), v[i].pic, buf, 128), ERROR_INVALID_FLAGS);
        T_TRUE(untouched_w(buf, 128));
    }
    /* LOCALE_NOUSEROVERRIDE with a picture is refused before the other flag questions: DATE_USE_ALT_CALENDAR
     * is otherwise not implemented. */
    fill_w(buf, 128);
    FAILS(rt_date_format(EN_US, LOCALE_NOUSEROVERRIDE | DATE_USE_ALT_CALENDAR, &may, W("yyyy"), buf, 128), ERROR_INVALID_FLAGS);
    T_TRUE(untouched_w(buf, 128));
    /* Repeated pictures. */
    OK_N(rt_date_format(EN_US, 0, &may, W("dd dd dd"), buf, 128), 9);
    T_TRUE(is_str(buf, "07 07 07"));
    /* A very long picture: the buffer is the only limit. */
    WCHAR longpic[1001];
    for (int i = 0; i < 1000; i++) longpic[i] = 'x';
    longpic[1000] = 0;
    static WCHAR bigbuf[1024];
    OK_N(rt_date_format(EN_US, 0, &may, longpic, bigbuf, 1024), 1001);
    OK_N(rt_date_format(EN_US, 0, &may, longpic, NULL, 0), 1001);
    FAILS(rt_date_format(EN_US, 0, &may, longpic, bigbuf, 1000), ERROR_INSUFFICIENT_BUFFER);
    /* Pictures the core refuses. */
    T_TRAPS(rt_date_format(EN_US, 0, &may, W("y"), buf, 128), "format picture \"y\"");
    T_TRAPS(rt_date_format(EN_US, 0, &may, W("dd y"), buf, 128), "GetDateFormatW: format picture \"y\"");
    T_TRAPS(rt_date_format(EN_US, 0, &may, W("g"), buf, 128), "format picture \"g\" (era)");
    T_TRAPS(rt_date_format(EN_US, 0, &may, W("dd gg yyyy"), buf, 128), "GetDateFormatW: format picture \"g\"");
    /* An empty picture is a picture (Wine's test_GetDateFormatW): flags 0 give the empty string,
     * 1 with the NUL, the size query too, and last error is left alone; a DATE_ flag with a
     * picture is ERROR_INVALID_FLAGS, buffer untouched; a NULL buffer with a length is
     * ERROR_INVALID_PARAMETER (the core's buffer check, first). */
    WCHAR ebuf[64];
    fill_w(ebuf, 64);
    OK_N(rt_date_format(EN_US, 0, &may, W(""), ebuf, 64), 1);
    T_TRUE(ebuf[0] == 0 && ebuf[1] == FILL);
    OK_N(rt_date_format(EN_US, 0, &may, W(""), NULL, 0), 1);
    fill_w(ebuf, 64);
    FAILS(rt_date_format(EN_US, DATE_LONGDATE, &may, W(""), ebuf, 64), ERROR_INVALID_FLAGS);
    T_TRUE(ebuf[0] == FILL && ebuf[63] == FILL);
    FAILS(rt_date_format(EN_US, 0, &may, W(""), NULL, 8), ERROR_INVALID_PARAMETER);
    OK_N(rt_date_format(EN_US, 0, &may, W("'y' 'g'"), buf, 128), 4);                  /* quoted, they are text */
    T_TRUE(is_str(buf, "y g"));
}

TEST(core, locale_date_flags)
{
    WCHAR buf[64];
    RtSystemTime t = st(2009, 2, 13, 0, 0, 0, 0);
    /* Format selectors exclude each other (documented). */
    FAILS(rt_date_format(EN_US, DATE_SHORTDATE | DATE_LONGDATE, &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    FAILS(rt_date_format(EN_US, DATE_SHORTDATE | DATE_YEARMONTH, &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    FAILS(rt_date_format(EN_US, DATE_LONGDATE | DATE_YEARMONTH, &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    FAILS(rt_date_format(EN_US, DATE_SHORTDATE | DATE_LONGDATE | DATE_YEARMONTH, &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    /* DATE_MONTHDAY is Windows 10: on the Windows 7 this machine reports it is no flag. */
    FAILS(rt_date_format(EN_US, DATE_MONTHDAY, &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    FAILS(rt_date_format(EN_US, DATE_MONTHDAY | DATE_LONGDATE, &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    /* Reading-order marks exclude each other; DATE_AUTOLAYOUT excludes both. */
    FAILS(rt_date_format(EN_US, DATE_LTRREADING | DATE_RTLREADING, &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    FAILS(rt_date_format(EN_US, DATE_AUTOLAYOUT | DATE_LTRREADING, &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    FAILS(rt_date_format(EN_US, DATE_AUTOLAYOUT | DATE_RTLREADING, &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    /* Bits that are no flag. */
    static const DWORD junk[] = { 0x100, 0x200, 0x1000, 0x10000, 0x08000000u, 0x10000000u, 0xFFFFFFFFu, 0x100 | DATE_LONGDATE };
    for (unsigned i = 0; i < sizeof junk / sizeof junk[0]; i++) FAILS(rt_date_format(EN_US, junk[i], &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    /* A format selector with a format picture: the selector is meaningless, ERROR_INVALID_FLAGS. */
    FAILS(rt_date_format(EN_US, DATE_SHORTDATE, &t, W("yyyy"), buf, 64), ERROR_INVALID_FLAGS);
    FAILS(rt_date_format(EN_US, DATE_LONGDATE, &t, W("yyyy"), buf, 64), ERROR_INVALID_FLAGS);
    FAILS(rt_date_format(EN_US, DATE_YEARMONTH, &t, W("yyyy"), buf, 64), ERROR_INVALID_FLAGS);
    OK_N(rt_date_format(EN_US, 0, &t, W("yyyy"), buf, 64), 5);
    /* Valid flags the core does not implement. */
    T_TRAPS(rt_date_format(EN_US, DATE_USE_ALT_CALENDAR, &t, NULL, buf, 64), "DATE_USE_ALT_CALENDAR");
    T_TRAPS(rt_date_format(EN_US, DATE_LTRREADING, &t, NULL, buf, 64), "DATE_LTRREADING");
    T_TRAPS(rt_date_format(EN_US, DATE_RTLREADING | DATE_LONGDATE, &t, NULL, buf, 64), "flags 0x22");
    T_TRAPS(rt_date_format(EN_US, DATE_AUTOLAYOUT, &t, NULL, buf, 64), "DATE_AUTOLAYOUT");
    /* Locales. */
    static const LCID others[] = { 0x0407, LOCALE_INVARIANT, 0x1000, 0x10409, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++) T_TRAPS(rt_date_format(others[i], 0, &t, NULL, buf, 64), "GetDateFormatW: locale");
    /* Locale 0 is the user default: the same text as en-US. */
    WCHAR en[64];
    int n_en = rt_date_format(EN_US, 0, &t, NULL, en, 64);
    T_TRUE(n_en > 1);
    OK_N(rt_date_format(LOCALE_NEUTRAL, 0, &t, NULL, buf, 64), n_en);
    T_TRUE(!memcmp(buf, en, (size_t)n_en * sizeof(WCHAR)));
    n_en = rt_time_format(EN_US, 0, &t, NULL, en, 64);
    T_TRUE(n_en > 1);
    OK_N(rt_time_format(LOCALE_NEUTRAL, 0, &t, NULL, buf, 64), n_en);
    T_TRUE(!memcmp(buf, en, (size_t)n_en * sizeof(WCHAR)));
    T_TRAPS(rt_date_format(0x0407, 0, &t, NULL, buf, 64), "locale 0x407");
    T_TRAPS(rt_time_format(0x0407, 0, &t, NULL, buf, 64), "GetTimeFormatW: locale 0x407");
}

TEST(core, locale_date_validity)
{
    WCHAR buf[64];
    /* The date must be a real date from 1601 to 9999. */
    static const struct { unsigned y, m, d; bool ok; } v[] = {
        { 1601, 1, 1, true }, { 1600, 12, 31, false }, { 1600, 1, 1, false }, { 0, 1, 1, false }, { 9999, 12, 31, true }, { 10000, 1, 1, false },
        { 30827, 12, 31, false }, { 65535, 1, 1, false },
        { 2001, 0, 1, false }, { 2001, 13, 1, false }, { 2001, 1, 0, false }, { 2001, 1, 31, true }, { 2001, 1, 32, false },
        { 2001, 2, 28, true }, { 2001, 2, 29, false }, { 2004, 2, 29, true }, { 2004, 2, 30, false },
        { 1900, 2, 29, false }, { 2000, 2, 29, true }, { 2100, 2, 29, false }, { 2400, 2, 29, true }, { 1700, 2, 29, false },
        { 2001, 4, 30, true }, { 2001, 4, 31, false }, { 2001, 6, 31, false }, { 2001, 9, 31, false }, { 2001, 11, 31, false },
        { 2001, 12, 31, true }, { 2001, 7, 31, true }, { 2001, 8, 31, true },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        RtSystemTime t = st(v[i].y, v[i].m, v[i].d, 0, 0, 0, 0);
        set_err();
        int n = rt_date_format(EN_US, 0, &t, NULL, buf, 64);
        if (v[i].ok) { if (n == 0 || T_LAST_ERROR() != SENTINEL) rt_test_fail(__FILE__, __LINE__, "%u-%u-%u should be valid", v[i].y, v[i].m, v[i].d); }
        else if (n != 0 || T_LAST_ERROR() != ERROR_INVALID_PARAMETER) rt_test_fail(__FILE__, __LINE__, "%u-%u-%u should be invalid", v[i].y, v[i].m, v[i].d);
        set_err();
        n = rt_date_format(EN_US, 0, &t, W("dd"), NULL, 0);                                  /* also with a picture and as a size query */
        T_EQ(n != 0, v[i].ok);
    }
    /* The time fields are ignored. */
    RtSystemTime t = st(2001, 1, 1, 99, 99, 99, 9999);
    OK_N(rt_date_format(EN_US, 0, &t, NULL, buf, 64), 9);
    T_TRUE(is_str(buf, "1/1/2001"));
    /* Every day of a leap year and a common year is valid, and no day after the end of the month is. */
    unsigned dim[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    for (unsigned m = 1; m <= 12; m++)
        for (unsigned d = 1; d <= 31; d++) {
            RtSystemTime x = st(2000, m, d, 0, 0, 0, 0);
            T_EQ(rt_date_format(EN_US, 0, &x, W("d"), NULL, 0) != 0, d <= dim[m - 1]);
            x.wYear = 2001;
            T_EQ(rt_date_format(EN_US, 0, &x, W("d"), NULL, 0) != 0, d <= (m == 2 ? 28 : dim[m - 1]));
        }
}

TEST(core, locale_date_buffers)
{
    WCHAR buf[64];
    RtSystemTime t = st(2009, 2, 13, 0, 0, 0, 0);
    /* The size query counts the NUL; len 0 does not use the buffer. */
    OK_N(rt_date_format(EN_US, 0, &t, NULL, NULL, 0), 10);
    OK_N(rt_date_format(EN_US, DATE_LONGDATE, &t, NULL, NULL, 0), 26);
    OK_N(rt_date_format(EN_US, 0, &t, NULL, buf, 0), 10);
    /* Exact fit; one short fails with the part that fits written (Windows's content after a failure is unspecified: Wine locale.c test_GetTimeFormatA: it "cannot be relied on"). */
    fill_w(buf, 64);
    OK_N(rt_date_format(EN_US, 0, &t, NULL, buf, 10), 10);
    T_TRUE(is_str(buf, "2/13/2009")); T_EQ(buf[10], FILL);
    fill_w(buf, 64);
    FAILS(rt_date_format(EN_US, 0, &t, NULL, buf, 9), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(buf[9], FILL);                                                         /* nothing beyond the buffer */
    FAILS(rt_date_format(EN_US, 0, &t, NULL, buf, 1), ERROR_INSUFFICIENT_BUFFER);
    FAILS(rt_date_format(EN_US, DATE_LONGDATE, &t, NULL, buf, 25), ERROR_INSUFFICIENT_BUFFER);
    OK_N(rt_date_format(EN_US, DATE_LONGDATE, &t, NULL, buf, 26), 26);
    /* Bad sizes and pointers. */
    FAILS(rt_date_format(EN_US, 0, &t, NULL, buf, -1), ERROR_INVALID_PARAMETER);
    FAILS(rt_date_format(EN_US, 0, &t, NULL, buf, INT_MIN), ERROR_INVALID_PARAMETER);
    FAILS(rt_date_format(EN_US, 0, &t, NULL, NULL, 1), ERROR_INVALID_PARAMETER);
    FAILS(rt_date_format(EN_US, 0, &t, NULL, NULL, 64), ERROR_INVALID_PARAMETER);
    /* Parameters are checked before the locale and the flags. */
    FAILS(rt_date_format(0x0407, 0xFFFFFFFFu, &t, NULL, NULL, 64), ERROR_INVALID_PARAMETER);
}

/* ============================================================================================
 * GetTimeFormatW
 * ========================================================================================== */

TEST(core, locale_time_default_and_flags)
{
    WCHAR buf[64];
    static const struct { unsigned h, mi, s; const char *def, *nosec, *nominsec, *nomarker, *f24, *f24nomarker, *nosec_nomarker, *all3; } v[] = {
        { 0, 0, 0, "12:00:00 AM", "12:00 AM", "12 AM", "12:00:00", "0:00:00 AM", "0:00:00", "12:00", "12" },
        { 1, 5, 9, "1:05:09 AM", "1:05 AM", "1 AM", "1:05:09", "1:05:09 AM", "1:05:09", "1:05", "1" },
        { 9, 5, 3, "9:05:03 AM", "9:05 AM", "9 AM", "9:05:03", "9:05:03 AM", "9:05:03", "9:05", "9" },
        { 11, 59, 59, "11:59:59 AM", "11:59 AM", "11 AM", "11:59:59", "11:59:59 AM", "11:59:59", "11:59", "11" },
        { 12, 0, 0, "12:00:00 PM", "12:00 PM", "12 PM", "12:00:00", "12:00:00 PM", "12:00:00", "12:00", "12" },
        { 13, 30, 45, "1:30:45 PM", "1:30 PM", "1 PM", "1:30:45", "13:30:45 PM", "13:30:45", "1:30", "1" },
        { 23, 59, 59, "11:59:59 PM", "11:59 PM", "11 PM", "11:59:59", "23:59:59 PM", "23:59:59", "11:59", "11" },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        RtSystemTime t = st(1999, 12, 31, v[i].h, v[i].mi, v[i].s, 777);          /* the date fields and milliseconds are ignored */
        t.wYear = 0; t.wMonth = 0; t.wDay = 0;                                     /* even invalid ones */
        static const LCID locales[] = { EN_US, LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT };
        for (unsigned l = 0; l < 3; l++) {
            LCID loc = locales[l];
            fill_w(buf, 64);
            OK_N(rt_time_format(loc, 0, &t, NULL, buf, 64), (int)strlen(v[i].def) + 1);
            T_TRUE(is_str(buf, v[i].def));
            OK_N(rt_time_format(loc, TIME_NOSECONDS, &t, NULL, buf, 64), (int)strlen(v[i].nosec) + 1);
            T_TRUE(is_str(buf, v[i].nosec));
            OK_N(rt_time_format(loc, TIME_NOMINUTESORSECONDS, &t, NULL, buf, 64), (int)strlen(v[i].nominsec) + 1);
            T_TRUE(is_str(buf, v[i].nominsec));
            OK_N(rt_time_format(loc, TIME_NOTIMEMARKER, &t, NULL, buf, 64), (int)strlen(v[i].nomarker) + 1);
            T_TRUE(is_str(buf, v[i].nomarker));
            OK_N(rt_time_format(loc, TIME_FORCE24HOURFORMAT, &t, NULL, buf, 64), (int)strlen(v[i].f24) + 1);
            T_TRUE(is_str(buf, v[i].f24));
            OK_N(rt_time_format(loc, TIME_FORCE24HOURFORMAT | TIME_NOTIMEMARKER, &t, NULL, buf, 64), (int)strlen(v[i].f24nomarker) + 1);
            T_TRUE(is_str(buf, v[i].f24nomarker));
            OK_N(rt_time_format(loc, TIME_NOSECONDS | TIME_NOTIMEMARKER, &t, NULL, buf, 64), (int)strlen(v[i].nosec_nomarker) + 1);
            T_TRUE(is_str(buf, v[i].nosec_nomarker));
            OK_N(rt_time_format(loc, TIME_NOMINUTESORSECONDS | TIME_NOTIMEMARKER, &t, NULL, buf, 64), (int)strlen(v[i].all3) + 1);
            T_TRUE(is_str(buf, v[i].all3));
            /* NOMINUTESORSECONDS includes NOSECONDS. */
            OK_N(rt_time_format(loc, TIME_NOMINUTESORSECONDS | TIME_NOSECONDS, &t, NULL, buf, 64), (int)strlen(v[i].nominsec) + 1);
            T_TRUE(is_str(buf, v[i].nominsec));
            /* The locale flags change nothing. */
            OK_N(rt_time_format(loc, LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP, &t, NULL, buf, 64), (int)strlen(v[i].def) + 1);
            T_TRUE(is_str(buf, v[i].def));
        }
    }
    /* All the hours: 12-hour clock with the right marker. */
    for (unsigned h = 0; h < 24; h++) {
        RtSystemTime t = st(2001, 1, 1, h, 0, 0, 0);
        OK_N(rt_time_format(EN_US, TIME_NOMINUTESORSECONDS, &t, NULL, buf, 64), h % 12 == 0 ? 6 : (h % 12 < 10 ? 5 : 6));
        T_EQ(buf[h % 12 == 0 || h % 12 >= 10 ? 3 : 2], h < 12 ? 'A' : 'P');
    }
    /* The size query. */
    RtSystemTime t = st(2001, 1, 1, 13, 30, 45, 0);
    OK_N(rt_time_format(EN_US, 0, &t, NULL, NULL, 0), 11);
    OK_N(rt_time_format(EN_US, TIME_FORCE24HOURFORMAT, &t, NULL, NULL, 0), 12);
    OK_N(rt_time_format(EN_US, 0, &t, NULL, buf, 0), 11);
}

TEST(core, locale_time_pictures)
{
    WCHAR buf[128];
    RtSystemTime pm = st(2001, 1, 1, 15, 4, 9, 123);
    RtSystemTime am = st(2001, 1, 1, 0, 4, 9, 0);
    static const struct { const WCHAR *pic; int t; const char *want; } v[] = {
        { u"h", 0, "3" }, { u"hh", 0, "03" }, { u"H", 0, "15" }, { u"HH", 0, "15" }, { u"h", 1, "12" }, { u"H", 1, "0" }, { u"HH", 1, "00" },
        { u"m", 0, "4" }, { u"mm", 0, "04" }, { u"s", 0, "9" }, { u"ss", 0, "09" },
        { u"t", 0, "P" }, { u"tt", 0, "PM" }, { u"t", 1, "A" }, { u"tt", 1, "AM" },
        /* A run longer than two counts as two. */
        { u"hhh", 0, "03" }, { u"HHHH", 1, "00" }, { u"mmm", 0, "04" }, { u"sss", 0, "09" }, { u"ttt", 0, "PM" }, { u"tttt", 1, "AM" },
        { u"hh':'mm':'ss tt", 0, "03:04:09 PM" }, { u"HH:mm:ss", 0, "15:04:09" }, { u"h.m.s t", 1, "12.4.9 A" },
        { u"hh':'mm':'ss tt", 1, "12:04:09 AM" },
        /* Quoting as in dates. */
        { u"'hh' hh", 0, "hh 03" }, { u"'o''clock' h", 0, "o'clock 3" }, { u"h 'unterminated mm", 0, "3 unterminated mm" },
        { u"HH'h'mm", 0, "15h04" }, { u"''", 0, "" },
        /* Date letters are plain text in a time picture, and so is anything else. */
        { u"d M y g", 0, "d M y g" }, { u"x h", 0, "x 3" }, { u"/ - . , : ; ( )", 0, "/ - . , : ; ( )" },
        { u"hms", 0, "3" "4" "9" }, { u"hh mm", 1, "12 04" },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        fill_w(buf, 128);
        int want = (int)strlen(v[i].want) + 1;
        set_err();
        int got = rt_time_format(EN_US, 0, (v[i].t ? &am : &pm), v[i].pic, buf, 128);
        if (got != want || !is_str(buf, v[i].want)) rt_test_fail(__FILE__, __LINE__, "picture #%u: got %d chars, expected \"%s\" (%d)", i, got, v[i].want, want);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        OK_N(rt_time_format(EN_US, 0, (v[i].t ? &am : &pm), v[i].pic, NULL, 0), want);
        /* LOCALE_NOUSEROVERRIDE with a picture is ERROR_INVALID_FLAGS; buffer untouched.  Windows:
         * Wine test_GetTimeFormatA, and MSDN ("LOCALE_NOUSEROVERRIDE can't be specified with a format string"). */
        fill_w(buf, 128);
        FAILS(rt_time_format(EN_US, LOCALE_NOUSEROVERRIDE, (v[i].t ? &am : &pm), v[i].pic, buf, 128), ERROR_INVALID_FLAGS);
        T_TRUE(untouched_w(buf, 128));
    }
    /* The hours of the 12-hour clock, h and hh, for all 24. */
    for (unsigned h = 0; h < 24; h++) {
        RtSystemTime t = st(2001, 1, 1, h, 0, 0, 0);
        unsigned h12 = h % 12 == 0 ? 12 : h % 12;
        OK_N(rt_time_format(EN_US, 0, &t, W("hh"), buf, 128), 3);
        T_EQ(buf[0], '0' + h12 / 10); T_EQ(buf[1], '0' + h12 % 10);
        OK_N(rt_time_format(EN_US, 0, &t, W("HH"), buf, 128), 3);
        T_EQ(buf[0], '0' + h / 10); T_EQ(buf[1], '0' + h % 10);
    }
    /* Flags with a picture: not verified, so refused. */
    T_TRAPS(rt_time_format(EN_US, TIME_NOSECONDS, &pm, W("h:mm:ss"), buf, 128), "together with a format picture");
    T_TRAPS(rt_time_format(EN_US, TIME_FORCE24HOURFORMAT, &pm, W("h"), buf, 128), "flags 0x8 together");
    /* LOCALE_NOUSEROVERRIDE with a picture is ERROR_INVALID_FLAGS, also with a TIME_* flag, which
     * is otherwise not implemented with a picture: the locale flag is checked first. */
    fill_w(buf, 128);
    FAILS(rt_time_format(EN_US, LOCALE_NOUSEROVERRIDE, &pm, W("h"), buf, 128), ERROR_INVALID_FLAGS);
    T_TRUE(untouched_w(buf, 128));
    fill_w(buf, 128);
    FAILS(rt_time_format(EN_US, LOCALE_NOUSEROVERRIDE | TIME_NOSECONDS, &pm, W("h:mm:ss"), buf, 128), ERROR_INVALID_FLAGS);
    T_TRUE(untouched_w(buf, 128));
    T_TRAPS(rt_time_format(EN_US, 0, &pm, W(""), buf, 128), "an empty format picture");
}

TEST(core, locale_time_validity_and_buffers)
{
    WCHAR buf[64];
    /* Range checks of every field. */
    static const struct { unsigned h, mi, s, ms; bool ok; } v[] = {
        { 0, 0, 0, 0, true }, { 23, 59, 59, 999, true }, { 24, 0, 0, 0, false }, { 0, 60, 0, 0, false }, { 0, 0, 60, 0, false }, { 0, 0, 0, 1000, false },
        { 23, 59, 59, 1000, false }, { 65535, 0, 0, 0, false }, { 12, 65535, 0, 0, false }, { 12, 0, 65535, 0, false }, { 12, 0, 0, 65535, false },
        { 12, 30, 30, 500, true }, { 24, 60, 60, 1000, false },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        RtSystemTime t = st(2001, 1, 1, v[i].h, v[i].mi, v[i].s, v[i].ms);
        set_err();
        int n = rt_time_format(EN_US, 0, &t, NULL, buf, 64);
        if (v[i].ok) { if (n == 0 || T_LAST_ERROR() != SENTINEL) rt_test_fail(__FILE__, __LINE__, "%u:%u:%u.%u should be valid", v[i].h, v[i].mi, v[i].s, v[i].ms); }
        else if (n != 0 || T_LAST_ERROR() != ERROR_INVALID_PARAMETER) rt_test_fail(__FILE__, __LINE__, "%u:%u:%u.%u should be invalid", v[i].h, v[i].mi, v[i].s, v[i].ms);
    }
    RtSystemTime t = st(2001, 1, 1, 13, 30, 45, 0);
    /* Flags. */
    static const DWORD junk[] = { 0x10, 0x20, 0x100, 0x1000, 0x10000, 0x08000000u, 0x10000000u, 0xFFFFFFFFu, 0x10 | TIME_NOSECONDS };
    for (unsigned i = 0; i < sizeof junk / sizeof junk[0]; i++) FAILS(rt_time_format(EN_US, junk[i], &t, NULL, buf, 64), ERROR_INVALID_FLAGS);
    /* Buffers. */
    fill_w(buf, 64);
    OK_N(rt_time_format(EN_US, 0, &t, NULL, buf, 11), 11);
    T_TRUE(is_str(buf, "1:30:45 PM")); T_EQ(buf[11], FILL);
    fill_w(buf, 64);
    FAILS(rt_time_format(EN_US, 0, &t, NULL, buf, 10), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(buf[10], FILL);
    FAILS(rt_time_format(EN_US, 0, &t, NULL, buf, 1), ERROR_INSUFFICIENT_BUFFER);
    FAILS(rt_time_format(EN_US, 0, &t, NULL, buf, -1), ERROR_INVALID_PARAMETER);
    FAILS(rt_time_format(EN_US, 0, &t, NULL, NULL, 1), ERROR_INVALID_PARAMETER);
    FAILS(rt_time_format(EN_US, 0, &t, NULL, NULL, 64), ERROR_INVALID_PARAMETER);
    FAILS(rt_time_format(0x0407, 0xFFFFFFFFu, &t, NULL, NULL, 64), ERROR_INVALID_PARAMETER);        /* parameters before locale and flags */
    static const LCID others[] = { 0x0407, LOCALE_INVARIANT, 0x1000, 0x10409, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++) T_TRAPS(rt_time_format(others[i], 0, &t, NULL, buf, 64), "GetTimeFormatW: locale");
}

TEST(core, locale_formats_agree_with_the_locale_table)
{
    /* GetLocaleInfoW hands out the pictures GetDateFormatW and GetTimeFormatW use by default: used as
     * an explicit picture they give the same result. */
    WCHAR pic[64], a[128], b[128];
    RtSystemTime d = st(2026, 10, 8, 17, 45, 9, 0);
    static const struct { LCTYPE type; DWORD flag; } dates[] = { { LOCALE_SSHORTDATE, DATE_SHORTDATE }, { LOCALE_SLONGDATE, DATE_LONGDATE }, { LOCALE_SYEARMONTH, DATE_YEARMONTH } };
    for (unsigned i = 0; i < 3; i++) {
        T_TRUE(rt_locale_info(EN_US, dates[i].type, pic, 64) > 0);
        int na = rt_date_format(EN_US, dates[i].flag, &d, NULL, a, 128);
        int nb = rt_date_format(EN_US, 0, &d, pic, b, 128);
        T_TRUE(na > 0 && na == nb && !memcmp(a, b, (size_t)na * 2));
    }
    T_TRUE(rt_locale_info(EN_US, LOCALE_STIMEFORMAT, pic, 64) > 0);
    int na = rt_time_format(EN_US, 0, &d, NULL, a, 128);
    int nb = rt_time_format(EN_US, 0, &d, pic, b, 128);
    T_TRUE(na > 0 && na == nb && !memcmp(a, b, (size_t)na * 2));
    T_TRUE(rt_locale_info(EN_US, LOCALE_SSHORTTIME, pic, 64) > 0);
    na = rt_time_format(EN_US, TIME_NOSECONDS, &d, NULL, a, 128);
    nb = rt_time_format(EN_US, 0, &d, pic, b, 128);
    T_TRUE(na > 0 && na == nb && !memcmp(a, b, (size_t)na * 2));
    /* The markers are LOCALE_S1159 and LOCALE_S2359. */
    WCHAR am[8], pm[8];
    T_TRUE(rt_locale_info(EN_US, LOCALE_S1159, am, 8) == 3 && rt_locale_info(EN_US, LOCALE_S2359, pm, 8) == 3);
    RtSystemTime morning = st(2001, 1, 1, 8, 0, 0, 0);
    OK_N(rt_time_format(EN_US, 0, &morning, W("tt"), a, 128), 3);
    T_TRUE(!memcmp(a, am, 4));
    OK_N(rt_time_format(EN_US, 0, &d, W("tt"), a, 128), 3);
    T_TRUE(!memcmp(a, pm, 4));
    /* The names are the table's. */
    for (unsigned i = 0; i < 7; i++) {
        WCHAR n[32];
        T_TRUE(rt_locale_info(EN_US, LOCALE_SDAYNAME1 + i, n, 32) > 0);
        RtSystemTime x = st(2001, 1, 1 + i, 0, 0, 0, 0);                 /* 2001-01-01 was a Monday */
        OK_N(rt_date_format(EN_US, 0, &x, W("dddd"), a, 128), (int)strlen(days[i]) + 1);
        T_TRUE(!memcmp(a, n, ((size_t)strlen(days[i]) + 1) * 2));
    }
}
