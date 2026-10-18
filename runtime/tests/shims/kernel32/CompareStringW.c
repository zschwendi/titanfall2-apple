#include "rt/test.h"
#include "rt/locale.h"
#include "rt/win/error.h"

#define EN_US  LANG_ENGLISH_US_LCID

/* The expected values are Windows's for these strings: lower-case letters, digits and prefixes,
 * whose order is the same by code unit and in Windows's linguistic order.  Strings that differ in
 * case are checked only for inequality without the ignore-case flags, because their order is
 * code-unit order here (see the shim). */
TEST(kernel32, CompareStringW)
{
    T_CASE("returns CSTR_LESS_THAN, CSTR_EQUAL and CSTR_GREATER_THAN");
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"abc", -1, u"abd", -1), CSTR_LESS_THAN);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"abd", -1, u"abc", -1), CSTR_GREATER_THAN);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"abc", -1, u"abc", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, LOCALE_USER_DEFAULT, 0, u"abc", -1, u"abc", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, LOCALE_SYSTEM_DEFAULT, 0, u"abc", -1, u"abd", -1), CSTR_LESS_THAN);
    /* A locale other than en-US is refused (the core stops on it). */
    T_TRAPS(CALL(kernel32, CompareStringW, 0x0407, 0, u"a", -1, u"a", -1), "a locale other than en-US: locale 0x407");

    T_CASE("locale 0 and LOCALE_INVARIANT compare as en-US");
    T_EQ(CALL(kernel32, CompareStringW, LOCALE_NEUTRAL, NORM_IGNORECASE, u"abc", -1, u"ABC", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, LOCALE_INVARIANT, NORM_IGNORECASE, u"abc", -1, u"ABC", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, LOCALE_NEUTRAL, 0, u"abc", -1, u"abd", -1), CSTR_LESS_THAN);
    T_EQ(CALL(kernel32, CompareStringW, LOCALE_INVARIANT, 0, u"abd", -1, u"abc", -1), CSTR_GREATER_THAN);
    T_EQ(CALL(kernel32, CompareStringW, LOCALE_INVARIANT, 0, u"abc", -1, u"abc", -1), CSTR_EQUAL);

    T_CASE("NORM_IGNORECASE and LINGUISTIC_IGNORECASE fold case, without them case counts");
    T_EQ(CALL(kernel32, CompareStringW, EN_US, NORM_IGNORECASE, u"Hello", -1, u"hELLO", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, LINGUISTIC_IGNORECASE, u"Hello", -1, u"hELLO", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, NORM_IGNORECASE, u"abc", -1, u"ABD", -1), CSTR_LESS_THAN);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, NORM_IGNORECASE, u"B", -1, u"a", -1), CSTR_GREATER_THAN);
    T_TRUE(CALL(kernel32, CompareStringW, EN_US, 0, u"Hello", -1, u"hELLO", -1) != CSTR_EQUAL);
    /* The flags the core does not implement are refused, each alone and with NORM_IGNORECASE. */
    T_TRAPS(CALL(kernel32, CompareStringW, EN_US, NORM_IGNORENONSPACE, u"a", -1, u"a", -1), "CompareStringW with NORM_IGNORENONSPACE");
    T_TRAPS(CALL(kernel32, CompareStringW, EN_US, NORM_IGNORESYMBOLS, u"a", -1, u"a", -1), "CompareStringW with NORM_IGNORESYMBOLS");
    T_TRAPS(CALL(kernel32, CompareStringW, EN_US, SORT_DIGITSASNUMBERS, u"a", -1, u"a", -1), "CompareStringW with SORT_DIGITSASNUMBERS");
    T_TRAPS(CALL(kernel32, CompareStringW, EN_US, LINGUISTIC_IGNOREDIACRITIC, u"a", -1, u"a", -1), "CompareStringW with LINGUISTIC_IGNOREDIACRITIC");
    T_TRAPS(CALL(kernel32, CompareStringW, EN_US, NORM_IGNOREKANATYPE, u"a", -1, u"a", -1), "CompareStringW with NORM_IGNOREKANATYPE");
    T_TRAPS(CALL(kernel32, CompareStringW, EN_US, NORM_IGNOREWIDTH, u"a", -1, u"a", -1), "CompareStringW with NORM_IGNOREWIDTH");
    T_TRAPS(CALL(kernel32, CompareStringW, EN_US, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, u"a", -1, u"a", -1), "CompareStringW with SORT_DIGITSASNUMBERS");
    T_TRAPS(CALL(kernel32, CompareStringW, EN_US, NORM_IGNORESYMBOLS, NULL, -1, u"a", -1), "CompareStringW with NORM_IGNORESYMBOLS");
    T_TRAPS(CALL(kernel32, CompareStringW, EN_US, NORM_IGNORECASE | NORM_IGNORESYMBOLS, u"a", -1, u"a", -1), "CompareStringW with NORM_IGNORESYMBOLS");

    T_CASE("a negative length means NUL-terminated, an explicit length is used as given");
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"abcdef", 3, u"abc", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"abXYZ", 2, u"ab", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"abc", -1, u"abc", 3), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"abcdef", 2, u"abc", -1), CSTR_LESS_THAN);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"abcdef", 4, u"abc", 3), CSTR_GREATER_THAN);

    T_CASE("a prefix sorts before the longer string");
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"a", -1, u"ab", -1), CSTR_LESS_THAN);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"ab", -1, u"a", -1), CSTR_GREATER_THAN);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"abc", 2, u"abc", 3), CSTR_LESS_THAN);

    T_CASE("SORT_STRINGSORT and NORM_LINGUISTIC_CASING change nothing");
    T_EQ(CALL(kernel32, CompareStringW, EN_US, SORT_STRINGSORT, u"abc", -1, u"abd", -1), CSTR_LESS_THAN);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, SORT_STRINGSORT, u"abc", -1, u"abc", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, NORM_LINGUISTIC_CASING, u"abc", -1, u"abc", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, NORM_LINGUISTIC_CASING | NORM_IGNORECASE, u"Hello", -1, u"hELLO", -1), CSTR_EQUAL);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, SORT_STRINGSORT | NORM_IGNORECASE, u"Hello", -1, u"hELLO", -1), CSTR_EQUAL);

    T_CASE("a NULL string returns 0 with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, NULL, -1, u"a", -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"a", -1, NULL, -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a flag bit outside the documented set returns 0 with ERROR_INVALID_FLAGS");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0x40, u"a", -1, u"a", -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, NORM_IGNORECASE | 0x80000000u, u"a", -1, u"a", -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"abc", -1, u"abd", -1), CSTR_LESS_THAN);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, NORM_IGNORECASE, u"Hello", -1, u"hELLO", -1), CSTR_EQUAL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    /* A success also leaves a code that a failure has set. */
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, NULL, -1, u"a", -1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(kernel32, CompareStringW, EN_US, 0, u"a", -1, u"a", -1), CSTR_EQUAL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
}
