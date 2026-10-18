/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-lcmapstringa
 * RT-CASES:  upper and lower case in en-US; size query; buffer one byte too small; in-place mapping; linguistic casing with a case flag; LOCALE_USE_CP_ACP changes nothing; parameter failures; flag failures; success leaves last error alone
 * RT-TRAPS:  LCMapStringA with more than 1024 characters; LCMapStringA result character outside code page 1252; core: LCMAP_TITLECASE; core: LCMAP_SORTKEY; core: LCMAP_BYTEREV; core: LCMAP_HASH; core: kana, width and Chinese flags; core: NORM_* flags; core: SORT_* flags; core: other flags; core: a locale other than en-US
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, int, LCMapStringA, LCID Locale, DWORD dwMapFlags, LPCSTR lpSrcStr, int cchSrc, LPSTR lpDestStr, int cchDest)
{
    WCHAR wide[1024], mapped[1024];
    BOOL used = FALSE;
    int n, m, k;

    if (!lpSrcStr || cchSrc == 0 || cchDest < 0)
        return FAIL(ctx, ERROR_INVALID_PARAMETER, 0);
    if (!lpDestStr && cchDest)
        return FAIL(ctx, ERROR_INVALID_PARAMETER, 0);

    n = rt_mb_to_wide(CP_ACP, 0, lpSrcStr, cchSrc, wide, 1024);
    if (n == 0) {
        if (rt_get_last_error(ctx) == ERROR_INSUFFICIENT_BUFFER)
            RT_UNIMPLEMENTED(ctx, "LCMapStringA with more than 1024 characters: %d", cchSrc);
        return 0;
    }

    /* LOCALE_USE_CP_ACP only picks the code page of the A form, which is CP_ACP here anyway. */
    m = rt_lcmap_string(Locale, dwMapFlags & ~LOCALE_USE_CP_ACP, wide, n, mapped, 1024);
    if (m == 0)
        return 0;
    if (cchDest == 0)
        return m;
    if (cchDest < m)
        return FAIL(ctx, ERROR_INSUFFICIENT_BUFFER, 0);

    k = rt_wide_to_mb(CP_ACP, WC_NO_BEST_FIT_CHARS, mapped, m, lpDestStr, cchDest, NULL, &used);
    if (k == 0)
        return 0;
    if (used) {
        for (int i = 0; i < m; i++) {
            char one = 0;
            BOOL u = FALSE;
            rt_wide_to_mb(CP_ACP, WC_NO_BEST_FIT_CHARS, &mapped[i], 1, &one, 1, NULL, &u);
            if (u)
                RT_UNIMPLEMENTED(ctx, "LCMapStringA result character outside code page 1252: U+%04X", (unsigned)mapped[i]);
        }
    }
    return m;
}
