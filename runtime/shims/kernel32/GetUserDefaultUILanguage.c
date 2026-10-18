/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-getuserdefaultuilanguage
 * RT-CASES:  returns the low 16 bits of the user default locale, en-US (0x0409); does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, LANGID, GetUserDefaultUILanguage)
{
    /* The function takes no arguments and has no failure.  It returns the language
     * identifier of the same locale as GetUserDefaultLCID: the low 16 bits of the core's
     * value.  The machine has no language interface pack, so the documented
     * LOCALE_CUSTOM_UI_DEFAULT return does not arise. */
    return (LANGID)(rt_locale_default() & 0xFFFF);
}
