/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-isvalidlocale
 * RT-CASES:  en-US with LCID_INSTALLED is TRUE; en-US with LCID_SUPPORTED is TRUE; LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT mean en-US; dwFlags 0 is a validity query: en-US and LOCALE_INVARIANT are TRUE, and LOCALE_NEUTRAL, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT are FALSE; any other dwFlags value is FALSE with ERROR_INVALID_FLAGS; success leaves last error alone
 * RT-TRAPS:  a locale other than en-US; dwFlags 0x39
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, BOOL, IsValidLocale, LCID Locale, DWORD dwFlags)
{
    /* Traps, where the documentation settles no answer.  0x39 is documented as "do not use";
     * it can behave like LCID_INSTALLED for some locales on some versions.  With LCID_INSTALLED
     * or LCID_SUPPORTED every locale other than en-US is a trap (LOCALE_INVARIANT, LOCALE_NEUTRAL
     * and LOCALE_CUSTOM_* included).  With dwFlags 0, a validity query (Wine
     * kernel32/tests/locale.c, test_LocaleNameToLCID), LOCALE_INVARIANT and LOCALE_NEUTRAL are
     * answered by the core as well; any other locale is a trap.
     * 0x0409 and the two *_DEFAULT values are en-US here, as the core reads them. */
    if (dwFlags == 0x39) RT_UNIMPLEMENTED(ctx, "IsValidLocale with dwFlags 0x39");
    if (Locale != 0x0409 && Locale != LOCALE_USER_DEFAULT && Locale != LOCALE_SYSTEM_DEFAULT
        && !(dwFlags == 0 && (Locale == LOCALE_NEUTRAL || Locale == LOCALE_INVARIANT)))
        RT_UNIMPLEMENTED(ctx, "IsValidLocale with a locale other than en-US: 0x%04X", (unsigned)Locale);

    /* The core answers for everything else and sets last error itself on failure, except for the
     * dwFlags 0 FALSE answers, which set none (the source gives no code). */
    return rt_locale_is_valid(Locale, dwFlags);
}
