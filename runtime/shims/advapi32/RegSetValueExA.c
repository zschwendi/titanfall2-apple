/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winreg/nf-winreg-regsetvalueexa
 * RT-CASES:  stores a REG_SZ, a REG_DWORD and a REG_BINARY value that the query reads back; a second set replaces the first value, type and data; the default value is set with a NULL or an empty name; a key without KEY_SET_VALUE, and HKEY_LOCAL_MACHINE, fail with ERROR_ACCESS_DENIED; a handle that is not a key fails with ERROR_INVALID_HANDLE; last error is never touched
 * RT-TRAPS:  core: Reserved is not 0; core: data NULL with cbData not 0; core: cbData above RT_REG_VALUE_DATA_MAX; core: a value name beyond RT_REG_VALUE_NAME_MAX; core: a predefined key the tree does not model (HKEY_PERFORMANCE_DATA, HKEY_PERFORMANCE_TEXT, HKEY_PERFORMANCE_NLSTEXT, HKEY_CURRENT_USER_LOCAL_SETTINGS)
 */
#include "rt/shim.h"
#include "rt/registry.h"

SHIM(advapi32, LSTATUS, RegSetValueExA, HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE *lpData, DWORD cbData)
{
    /* The core does all of it (rt_reg_set_value_ex_a, rt/registry.h): it converts the name and the
     * data of a string type from the ANSI code page, checks KEY_SET_VALUE and the two keys that
     * refuse writes (HKEY_LOCAL_MACHINE, HKEY_USERS), and saves the file.  It returns the status
     * and sets no last error, so neither does the shim.
     *
     * The core stops (fatal) on a nonzero Reserved, on lpData NULL with cbData nonzero, on cbData
     * above RT_REG_VALUE_DATA_MAX, on a name beyond RT_REG_VALUE_NAME_MAX, and on the predefined
     * keys it does not model.  The shim adds no guard for those: a guard would run before the
     * core's own checks and change which failure the guest sees. */
    return rt_reg_set_value_ex_a(hKey, lpValueName, Reserved, dwType, lpData, cbData);
}
