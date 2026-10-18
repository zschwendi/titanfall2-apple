/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-enumsystemlocalesw
 * RT-CASES:  calls the callback once with the name 00000409; a callback returning FALSE: the function still returns TRUE; flags the core rejects fail with ERROR_INVALID_FLAGS; a NULL callback fails with ERROR_INVALID_PARAMETER; success leaves last error alone
 * RT-TRAPS:  LCID_ALTERNATE_SORTS
 */
#include "rt/shim.h"
#include "rt/locale.h"

/* The core lists the locales; the shim runs the loop that calls the guest callback, as the
 * comment above rt_locale_enum_begin gives it.
 *
 * A NULL callback fails with ERROR_INVALID_PARAMETER: the documentation's code for "any of the
 * parameter values was invalid".  The flags are checked by the core, which sets
 * ERROR_INVALID_FLAGS.  The check comes before the alternate-sort refusal below, so a bad flags
 * value is still reported as the documentation says.
 *
 * LCID_ALTERNATE_SORTS is refused.  The documentation says that it returns the alternate sort
 * identifiers, but not which ones exist on this machine; the core's empty list for it is its own
 * claim, and Windows lists some (Wine locale.c test_EnumSystemLocalesA), so the shim does not
 * return it as if it were Windows's answer. */
SHIM(kernel32, BOOL, EnumSystemLocalesW, LOCALE_ENUMPROCW lpLocaleEnumProc, DWORD dwFlags)
{
    RtLocaleEnum e;
    WCHAR name[RT_LOCALE_ENUM_NAME_MAX];

    if (!lpLocaleEnumProc) return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);
    if (!rt_locale_enum_begin(dwFlags, &e)) return FALSE;        /* the core has set ERROR_INVALID_FLAGS */
    if (dwFlags & LCID_ALTERNATE_SORTS)
        RT_UNIMPLEMENTED(ctx, "EnumSystemLocalesW with LCID_ALTERNATE_SORTS (flags %#x)", dwFlags);
    while (rt_locale_enum_next(&e, name))
        if ((DWORD)GUEST_CALL(ctx, lpLocaleEnumProc, name) == 0) break;
    return TRUE;
}
