/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getstringtypea
 * RT-CASES:  CT_CTYPE1 of a1 with a length of 3 gives the words of rt_string_type on the wide string; a length of -1 also writes the terminator's word; every byte of code page 1252 gives the words of rt_string_type on its wide character; byte 0xE9 has the type of U+00E9; the locales 0x0409, 0, LOCALE_INVARIANT, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT all mean code page 1252; ERROR_INVALID_PARAMETER for info type 0; ERROR_INVALID_FLAGS for an info type with a bit outside the three; ERROR_INVALID_PARAMETER for a NULL source or output, length 0 and out equal to src; does not change last error on success
 * RT-TRAPS:  locale other than 0x0409, 0, LOCALE_INVARIANT, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT; more than 1024 characters
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, BOOL, GetStringTypeA, LCID Locale, DWORD dwInfoType, LPCSTR lpSrcStr, int cchSrc, LPWORD lpCharType)
{
    WCHAR wide[1024];
    int n;

    /* The documented argument failures come before the conversion.  The source and the output
     * must not be the same pointer (GetStringTypeA, Remarks). */
    if (!lpSrcStr || cchSrc == 0 || (const void *)lpSrcStr == (const void *)lpCharType)
        return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);
    /* The locale only selects the code page of the text, and only code page 1252 is carried.
     * The five locales accepted here (0x0409, 0, LOCALE_INVARIANT, LOCALE_USER_DEFAULT,
     * LOCALE_SYSTEM_DEFAULT) all give code page 1252; the core takes no locale here. */
    if (Locale != 0x0409 && Locale != 0 && Locale != LOCALE_INVARIANT
        && Locale != LOCALE_USER_DEFAULT && Locale != LOCALE_SYSTEM_DEFAULT)
        RT_UNIMPLEMENTED(ctx, "GetStringTypeA: locale other than 0x0409, 0, LOCALE_INVARIANT, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT (0x%x) is not carried", (unsigned)Locale);
    /* Code page 1252 maps one byte to one WCHAR.  A negative cchSrc is a NUL-terminated string,
     * and its NUL is converted too, so n counts the terminator's word. */
    n = rt_mb_to_wide(CP_ACP, 0, lpSrcStr, cchSrc, wide, 1024);
    if (n == 0) {
        /* The core has set the last error. */
        if (rt_get_last_error(ctx) == ERROR_INSUFFICIENT_BUFFER)
            RT_UNIMPLEMENTED(ctx, "GetStringTypeA string of more than 1024 characters (cchSrc %d)", cchSrc);
        return FALSE;
    }
    return rt_string_type(dwInfoType, wide, n, lpCharType);
}
