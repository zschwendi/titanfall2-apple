/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-lcmapstringw
 * RT-CASES:  upper and lower case in en-US; size query; buffer one too small; source length -1 and explicit length; in-place mapping; linguistic casing with a case flag; parameter failures; flag failures
 * RT-TRAPS:  core: LCMAP_TITLECASE; core: LCMAP_SORTKEY; core: LCMAP_BYTEREV; core: LCMAP_HASH; core: kana, width and Chinese flags; core: NORM_* flags; core: other flags
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, int, LCMapStringW, LCID Locale, DWORD dwMapFlags, LPCWSTR lpSrcStr, int cchSrc, LPWSTR lpDestStr, int cchDest)
{
    return rt_lcmap_string(Locale, dwMapFlags, lpSrcStr, cchSrc, lpDestStr, cchDest);
}
