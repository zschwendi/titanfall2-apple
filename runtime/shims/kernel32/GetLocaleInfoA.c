/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-getlocaleinfoa
 * RT-CASES:  LOCALE_SNAME as text: en-US and its NUL; LOCALE_SENGLANGUAGE and LOCALE_SDECIMAL as text: the core's text, same length and characters; the size query returns the length with the NUL and writes nothing; a buffer one byte too small fails with ERROR_INSUFFICIENT_BUFFER and is filled up to its size; LOCALE_IDEFAULTANSICODEPAGE with LOCALE_RETURN_NUMBER: 1252 stored in 4 bytes; the size query with LOCALE_RETURN_NUMBER returns 4 and writes nothing; LOCALE_RETURN_NUMBER into a buffer under 4 bytes fails with ERROR_INSUFFICIENT_BUFFER and writes nothing; a negative length fails with ERROR_INVALID_PARAMETER and writes nothing; a NULL buffer with a nonzero length fails with ERROR_INVALID_PARAMETER; LOCALE_NOUSEROVERRIDE and LOCALE_USE_CP_ACP change nothing; locale 0, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT are en-US; success leaves last error alone
 * RT-TRAPS:  core: a locale other than en-US; core: LOCALE_RETURN_GENITIVE_NAMES; core: LOCALE_RETURN_NUMBER with a type that has no number; core: an LCType not in the table; a table entry longer than 255 characters; a character outside code page 1252
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, int, GetLocaleInfoA, LCID Locale, LCTYPE LCType, LPSTR lpLCData, int cchData)
{
    /* Step 1: a negative length, and a NULL buffer with a nonzero length (the notes). */
    if (cchData < 0 || (lpLCData == NULL && cchData != 0))
        return FAIL(ctx, ERROR_INVALID_PARAMETER, 0);

    /* Step 2: a number is a DWORD, 4 bytes in this form. */
    if (LCType & LOCALE_RETURN_NUMBER) {
        DWORD value = 0;
        if (!rt_locale_info(Locale, LCType, (WCHAR *)&value, 2))
            return 0;
        if (cchData == 0)
            return 4;
        if (cchData < 4)
            return FAIL(ctx, ERROR_INSUFFICIENT_BUFFER, 0);
        const unsigned char *p = (const unsigned char *)&value;
        for (int i = 0; i < 4; i++)
            lpLCData[i] = (char)p[i];
        return 4;
    }

    /* Step 3: the text, from the wide core.  No entry of the table is 256 WCHARs long. */
    WCHAR wide[256];
    int m = rt_locale_info(Locale, LCType, wide, 256);
    if (m == 0) {
        if (rt_get_last_error(ctx) == ERROR_INSUFFICIENT_BUFFER)
            RT_UNIMPLEMENTED(ctx, "GetLocaleInfoA type %#x: a table entry longer than 255 characters", (unsigned)LCType);
        return 0;
    }

    /* Steps 4 to 6: the size query, then the conversion to code page 1252.  A buffer that is
     * too small is filled with its first cchData characters, and then the call fails. */
    if (cchData == 0)
        return m;
    int take = cchData < m ? cchData : m;
    BOOL used = FALSE;
    if (!rt_wide_to_mb(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, take, lpLCData, cchData, NULL, &used))
        return 0;
    if (used)
        RT_UNIMPLEMENTED(ctx, "GetLocaleInfoA type %#x: a character outside code page 1252", (unsigned)LCType);
    if (cchData < m)
        return FAIL(ctx, ERROR_INSUFFICIENT_BUFFER, 0);
    return m;
}
