/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-getuserdefaultlcid
 * RT-CASES:  returns the user default locale, en-US (0x0409); does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, LCID, GetUserDefaultLCID)
{
    /* The function takes no arguments and has no failure.  The machine's user default
     * locale is en-US, which is not a custom locale, so the documented LOCALE_CUSTOM_DEFAULT
     * return does not arise. */
    return rt_locale_default();
}
