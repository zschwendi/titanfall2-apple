/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winreg/nf-winreg-regdeletevaluea
 * RT-CASES:  deletes a value, so it is gone and a second delete is ERROR_FILE_NOT_FOUND; the default value is deleted with a NULL or an empty name; a key without KEY_SET_VALUE fails with ERROR_ACCESS_DENIED and keeps the value; a handle that is not a key fails with ERROR_INVALID_HANDLE; last error is never touched
 * RT-TRAPS:  core: deleting a seeded value; core: a value name beyond RT_REG_VALUE_NAME_MAX; core: a predefined key the tree does not model (HKEY_PERFORMANCE_DATA, HKEY_PERFORMANCE_TEXT, HKEY_PERFORMANCE_NLSTEXT, HKEY_CURRENT_USER_LOCAL_SETTINGS)
 */
#include "rt/shim.h"
#include "rt/registry.h"

SHIM(advapi32, LSTATUS, RegDeleteValueA, HKEY hKey, LPCSTR lpValueName)
{
    /* The core does all of it (rt_reg_delete_value_a, rt/registry.h): it converts the name from the
     * ANSI code page, checks KEY_SET_VALUE, finds the value and saves the file.  It returns the
     * status and sets no last error, so neither does the shim.
     *
     * The core stops (fatal) on deleting a seeded value (it would come back at the next start), on
     * a name beyond RT_REG_VALUE_NAME_MAX, and on the predefined keys it does not model.  The shim
     * adds no guard for those: a guard would run before the core's own checks and change which
     * failure the guest sees. */
    return rt_reg_delete_value_a(hKey, lpValueName);
}
