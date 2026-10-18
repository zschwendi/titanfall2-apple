/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lstrcmpa
 * RT-CASES:  returns -1, 0 and 1 in the order of the strings; a prefix sorts first, and the empty string sorts before a non-empty one; both strings NULL returns 0; only the first NULL returns -1; only the second NULL returns 1; does not change last error on success; unequal strings outside 0-9a-z: code-unit order, one warning
 * RT-TRAPS:  a string of 1024 characters or more
 */
#include "rt/shim.h"
#include "rt/locale.h"

/* The page: lstrcmpA calls CompareStringEx with the thread locale and subtracts 2 from the result.
 * The core compares code units, not Windows's linguistic order (see rt_compare_string): equality
 * is exact, but the order of two unequal strings may differ from Windows's ("a" sorts after "B"
 * here).  The first time such a pair has a byte outside 0-9 and a-z, one warning is logged; the
 * code-unit result is returned as before.  The strings are converted from CP_ACP on the stack,
 * with their NUL, which takes one of the 1024 WCHARs: a string of 1024 characters or more does
 * not fit and is refused. */

/* True if s has a byte outside 0-9 and a-z: the bytes where code-unit order may differ from
 * Windows's word sort. */
static int has_byte_outside_word_set(LPCSTR s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))) return 1;
    }
    return 0;
}

SHIM(kernel32, int, lstrcmpA, LPCSTR lpString1, LPCSTR lpString2)
{
    WCHAR w1[1024], w2[1024];
    int n, r;

    /* NULL strings are not on the page: Wine's locale tests assert these results.  No conversion
     * and no core call. */
    if (!lpString1 && !lpString2) return 0;
    if (!lpString1) return -1;
    if (!lpString2) return 1;

    /* The core sets the last error when the conversion fails; the shim only looks at it, to tell
     * a string that does not fit from any other failure. */
    n = rt_mb_to_wide(CP_ACP, 0, lpString1, -1, w1, 1024);
    if (n == 0) {
        if (rt_get_last_error(ctx) == ERROR_INSUFFICIENT_BUFFER)
            RT_UNIMPLEMENTED(ctx, "lstrcmpA with a string of 1024 characters or more");
        return 0;
    }
    n = rt_mb_to_wide(CP_ACP, 0, lpString2, -1, w2, 1024);
    if (n == 0) {
        if (rt_get_last_error(ctx) == ERROR_INSUFFICIENT_BUFFER)
            RT_UNIMPLEMENTED(ctx, "lstrcmpA with a string of 1024 characters or more");
        return 0;
    }

    r = rt_compare_string(rt_locale_default(), 0, w1, -1, w2, -1);
    if (r != CSTR_EQUAL && (has_byte_outside_word_set(lpString1) ||
                            has_byte_outside_word_set(lpString2)))
        rt_log_once(RT_CH_IMPORT, RT_LL_WARN, "kernel32.dll!lstrcmpA",
                    "lstrcmpA: unequal strings are ordered by code unit, not by Windows's word sort");
    return r - 2;
}
