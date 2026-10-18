/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/datetimeapi/nf-datetimeapi-getdateformatw
 * RT-CASES:  the short date with flags 0; the long date with DATE_LONGDATE and the year-month with DATE_YEARMONTH; a format picture, with its letters and quoted text; an empty format picture gives the empty string with flags 0, and ERROR_INVALID_FLAGS with a DATE_ format flag; the size query returns the length with the NUL and writes nothing; a buffer one too small fails with ERROR_INSUFFICIENT_BUFFER; a NULL date formats the current date; an invalid date fails with ERROR_INVALID_PARAMETER; a bad flag fails with ERROR_INVALID_FLAGS; DATE_LTRREADING with DATE_RTLREADING fails with ERROR_INVALID_FLAGS; a negative length fails with ERROR_INVALID_PARAMETER; the default locales and the LOCALE_ flags are en-US; locale 0 gives the same result as 0x0409; does not change last error
 * RT-TRAPS:  a locale other than en-US; DATE_USE_ALT_CALENDAR; DATE_LTRREADING; DATE_RTLREADING; DATE_AUTOLAYOUT; a single unquoted y; an unquoted g; a NULL buffer with a nonzero length
 */
#include "rt/shim.h"
#include "rt/locale.h"

/* en-US as the machine has it (RUNTIME 8.4): 0x0409, the user and system defaults, which
 * are en-US here, and locale 0 (LOCALE_NEUTRAL), which the core reads as the user default
 * for this function.  Anything else is a trap. */
static int is_en_us(LCID locale) {
    return locale == LANG_ENGLISH_US_LCID || locale == LOCALE_USER_DEFAULT
        || locale == LOCALE_SYSTEM_DEFAULT || locale == LOCALE_NEUTRAL;
}

/* The offset of the first picture letter this shim does not format, or -1.  The letters are
 * g (any run), and a single y (a run of two or more y is the year, as the core formats it).
 * Quoted text is skipped the way the core reads quotes: '' inside quotes is one quote. */
static int first_unsupported_letter(const WCHAR *pic) {
    for (unsigned i = 0; pic[i];) {
        if (pic[i] == '\'') {
            i++;
            while (pic[i]) {
                if (pic[i] == '\'') {
                    if (pic[i + 1] == '\'') { i += 2; continue; }
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        unsigned r = 1;
        while (pic[i + r] == pic[i]) r++;
        if (pic[i] == 'g' || (pic[i] == 'y' && r == 1)) return (int)i;
        i += r;
    }
    return -1;
}

SHIM(kernel32, int, GetDateFormatW, LCID Locale, DWORD dwFlags, const SYSTEMTIME *lpDate, LPCWSTR lpFormat, LPWSTR lpDateStr, int cchDate)
{
    /* The cases refused before the core runs: the ones the documentation leaves open, and the
     * NULL buffer, where the documentation (ERROR_INSUFFICIENT_BUFFER) and the core
     * (ERROR_INVALID_PARAMETER) disagree.  Every other argument goes to the core, which makes
     * the documented checks and the formatting. */
    if (!is_en_us(Locale))
        RT_UNIMPLEMENTED(ctx, "GetDateFormatW with locale %#x: a locale other than en-US is not implemented", (unsigned)Locale);
    /* DATE_LTRREADING with DATE_RTLREADING is the documented ERROR_INVALID_FLAGS, which the core
     * gives; one of the two alone is not implemented. */
    if ((dwFlags & (DATE_USE_ALT_CALENDAR | DATE_AUTOLAYOUT)) || ((dwFlags & DATE_LTRREADING) != 0) != ((dwFlags & DATE_RTLREADING) != 0))
        RT_UNIMPLEMENTED(ctx, "GetDateFormatW with flags %#x: DATE_USE_ALT_CALENDAR, DATE_AUTOLAYOUT, and DATE_LTRREADING or DATE_RTLREADING alone are not implemented", (unsigned)dwFlags);
    if (lpDateStr == NULL && cchDate > 0)
        RT_UNIMPLEMENTED(ctx, "GetDateFormatW: a NULL buffer with a nonzero length (%d): the documentation says ERROR_INSUFFICIENT_BUFFER and the core says ERROR_INVALID_PARAMETER", cchDate);
    if (lpFormat) {
        int at = first_unsupported_letter(lpFormat);
        if (at >= 0 && lpFormat[at] == 'g')
            RT_UNIMPLEMENTED(ctx, "GetDateFormatW: an unquoted g at picture offset %d: the era string was not verified", at);
        if (at >= 0)
            RT_UNIMPLEMENTED(ctx, "GetDateFormatW: a single unquoted y at picture offset %d: the documentation contradicts itself on y", at);
    }
    return rt_date_format(Locale, dwFlags, lpDate, lpFormat, lpDateStr, cchDate);
}
