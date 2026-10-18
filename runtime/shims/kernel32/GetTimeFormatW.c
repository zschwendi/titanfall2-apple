/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/datetimeapi/nf-datetimeapi-gettimeformatw
 * RT-CASES:  the locale format h:mm:ss tt with flags 0; TIME_NOSECONDS, TIME_NOTIMEMARKER, TIME_NOMINUTESORSECONDS and TIME_FORCE24HOURFORMAT; a format picture, with its letters and quoted text; the size query returns the length with the NUL and writes nothing; a buffer one too small fails with ERROR_INSUFFICIENT_BUFFER; a NULL time formats the current time; an invalid time fails with ERROR_INVALID_PARAMETER; a bad flag fails with ERROR_INVALID_FLAGS; a negative length fails with ERROR_INVALID_PARAMETER; the default locales and the LOCALE_ flags are en-US; locale 0 gives the same result as 0x0409; does not change last error
 * RT-TRAPS:  a locale other than en-US; an empty format picture; TIME_* flags with a format picture; a NULL buffer with a nonzero length
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

SHIM(kernel32, int, GetTimeFormatW, LCID Locale, DWORD dwFlags, const SYSTEMTIME *lpTime, LPCWSTR lpFormat, LPWSTR lpTimeStr, int cchTime)
{
    /* The cases refused before the core runs: the ones the documentation leaves open, and the
     * NULL buffer, where the documentation (ERROR_INSUFFICIENT_BUFFER) and the core
     * (ERROR_INVALID_PARAMETER) disagree.  Every other argument goes to the core, which makes
     * the documented checks and the formatting. */
    if (!is_en_us(Locale))
        RT_UNIMPLEMENTED(ctx, "GetTimeFormatW with locale %#x: a locale other than en-US is not implemented", (unsigned)Locale);
    if (lpFormat && lpFormat[0] == 0)
        RT_UNIMPLEMENTED(ctx, "GetTimeFormatW: an empty format picture: whether Windows treats it as NULL or as an empty result was not verified");
    if (lpFormat && (dwFlags & (TIME_NOMINUTESORSECONDS | TIME_NOSECONDS | TIME_NOTIMEMARKER | TIME_FORCE24HOURFORMAT)))
        RT_UNIMPLEMENTED(ctx, "GetTimeFormatW: TIME_* flags with a format picture (flags %#x): what Windows does with that pair was not verified", (unsigned)dwFlags);
    if (lpTimeStr == NULL && cchTime > 0)
        RT_UNIMPLEMENTED(ctx, "GetTimeFormatW: a NULL buffer with a nonzero length (%d): the documentation says ERROR_INSUFFICIENT_BUFFER and the core says ERROR_INVALID_PARAMETER", cchTime);
    return rt_time_format(Locale, dwFlags, lpTime, lpFormat, lpTimeStr, cchTime);
}
