/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringw
 * RT-CASES:  returns CSTR_LESS_THAN, CSTR_EQUAL and CSTR_GREATER_THAN; NORM_IGNORECASE and LINGUISTIC_IGNORECASE fold case, without them case counts; a negative length means NUL-terminated, an explicit length is used as given; a prefix sorts before the longer string; SORT_STRINGSORT and NORM_LINGUISTIC_CASING change nothing; locale 0 and LOCALE_INVARIANT compare as en-US; a NULL string returns 0 with ERROR_INVALID_PARAMETER; a flag bit outside the documented set returns 0 with ERROR_INVALID_FLAGS; does not change last error on success
 * RT-TRAPS:  NORM_IGNORENONSPACE; NORM_IGNORESYMBOLS; NORM_IGNOREKANATYPE; NORM_IGNOREWIDTH; SORT_DIGITSASNUMBERS; LINGUISTIC_IGNOREDIACRITIC; a locale other than en-US
 */
#include "rt/shim.h"
#include "rt/locale.h"

/* Each refused case is checked here, before the core call, and names its own flag, so the
 * report says which one.  The core is fatal for the same cases; the shim refuses them first
 * whatever the strings and the other bits are, because the answer for them is not known. */
SHIM(kernel32, int, CompareStringW, LCID Locale, DWORD dwCmpFlags, LPCWCH lpString1, int cchCount1, LPCWCH lpString2, int cchCount2)
{
    if (dwCmpFlags & NORM_IGNORENONSPACE)
        RT_UNIMPLEMENTED(ctx, "CompareStringW with NORM_IGNORENONSPACE");
    if (dwCmpFlags & NORM_IGNORESYMBOLS)
        RT_UNIMPLEMENTED(ctx, "CompareStringW with NORM_IGNORESYMBOLS");
    if (dwCmpFlags & NORM_IGNOREKANATYPE)
        RT_UNIMPLEMENTED(ctx, "CompareStringW with NORM_IGNOREKANATYPE");
    if (dwCmpFlags & NORM_IGNOREWIDTH)
        RT_UNIMPLEMENTED(ctx, "CompareStringW with NORM_IGNOREWIDTH");
    if (dwCmpFlags & SORT_DIGITSASNUMBERS)
        RT_UNIMPLEMENTED(ctx, "CompareStringW with SORT_DIGITSASNUMBERS");
    if (dwCmpFlags & LINGUISTIC_IGNOREDIACRITIC)
        RT_UNIMPLEMENTED(ctx, "CompareStringW with LINGUISTIC_IGNOREDIACRITIC");
    /* en-US is LANG_ENGLISH_US_LCID, and the two *_DEFAULT values that rt_locale_default
     * resolves to it on this machine (as rt_compare_string takes them).  Locale 0 (LOCALE_NEUTRAL)
     * is read as the user default, and LOCALE_INVARIANT compares as en-US: rt_compare_string takes
     * both. */
    if (Locale != LANG_ENGLISH_US_LCID && Locale != LOCALE_USER_DEFAULT && Locale != LOCALE_SYSTEM_DEFAULT
        && Locale != LOCALE_NEUTRAL && Locale != LOCALE_INVARIANT)
        RT_UNIMPLEMENTED(ctx, "CompareStringW with a locale other than en-US: locale %#x", Locale);
    /* A NULL string and a bit outside the documented flags are documented failures: the core
     * returns them.  Caveat, from the core: for two unequal strings without an ignore-case flag
     * the result is code-unit order, which is not Windows's linguistic order ("a" is greater than
     * "B" here, less on Windows).  Only the equality answer is right in general; the shim does not
     * refuse those results. */
    return rt_compare_string(Locale, dwCmpFlags, lpString1, cchCount1, lpString2, cchCount2);
}
