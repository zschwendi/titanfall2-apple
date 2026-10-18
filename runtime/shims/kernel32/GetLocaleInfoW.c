/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-getlocaleinfow
 * RT-CASES:  LOCALE_SNAME as text: en-US and its NUL; LOCALE_SDECIMAL as text: the decimal point; LOCALE_IDEFAULTANSICODEPAGE as text: 1252; LOCALE_IDEFAULTANSICODEPAGE with LOCALE_RETURN_NUMBER: the DWORD 1252 in two WCHARs; the size query returns the length with the NUL and writes nothing; the size query with LOCALE_RETURN_NUMBER returns 2 and writes nothing; a buffer one too small fails with ERROR_INSUFFICIENT_BUFFER and writes nothing; a negative length fails with ERROR_INVALID_PARAMETER and writes nothing; LOCALE_NOUSEROVERRIDE and LOCALE_USE_CP_ACP change nothing; locale 0, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT are en-US; success leaves last error alone
 * RT-TRAPS:  a locale other than en-US; LOCALE_RETURN_GENITIVE_NAMES; a NULL buffer with a nonzero length
 */
#include "rt/shim.h"
#include "rt/locale.h"

/* en-US as the machine has it (RUNTIME 8.4): 0x0409, the user and system defaults, and
 * locale 0 (LOCALE_NEUTRAL), which the core reads as the user default for this function.
 * Anything else is a trap. */
static int is_en_us(LCID locale) {
    return locale == LANG_ENGLISH_US_LCID || locale == LOCALE_USER_DEFAULT
        || locale == LOCALE_SYSTEM_DEFAULT || locale == LOCALE_NEUTRAL;
}

SHIM(kernel32, int, GetLocaleInfoW, LCID Locale, LCTYPE LCType, LPWSTR lpLCData, int cchData)
{
    /* The cases refused before the core runs: a locale the machine does not have, the
     * genitive-name flag (its strings were not verified), and the NULL buffer, where the
     * documentation (ERROR_INSUFFICIENT_BUFFER) and the core (ERROR_INVALID_PARAMETER)
     * disagree.  Every other argument goes to the core, which makes the documented checks
     * and reads the table. */
    if (!is_en_us(Locale))
        RT_UNIMPLEMENTED(ctx, "GetLocaleInfoW with locale %#x: a locale other than en-US is not implemented", (unsigned)Locale);
    if (LCType & LOCALE_RETURN_GENITIVE_NAMES)
        RT_UNIMPLEMENTED(ctx, "GetLocaleInfoW with type %#x: LOCALE_RETURN_GENITIVE_NAMES is not implemented", (unsigned)LCType);
    if (lpLCData == NULL && cchData > 0)
        RT_UNIMPLEMENTED(ctx, "GetLocaleInfoW: a NULL buffer with a nonzero length (%d): the documentation says ERROR_INSUFFICIENT_BUFFER and the core says ERROR_INVALID_PARAMETER", cchData);
    return rt_locale_info(Locale, LCType, lpLCData, cchData);
}
