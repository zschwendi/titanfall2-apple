/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winreg/nf-winreg-regopenkeyexa
 * RT-CASES:  opens an existing key as a new handle; a missing key fails with ERROR_FILE_NOT_FOUND and leaves *phkResult alone; a NULL or empty subkey gives the predefined key itself, or a new handle under an open key; a trailing backslash is ignored; a leading backslash fails with ERROR_BAD_PATHNAME, or is skipped under HKEY_CLASSES_ROOT; a parent that is not open fails with ERROR_INVALID_HANDLE; a deleted parent fails with ERROR_KEY_DELETED; a NULL result fails with ERROR_INVALID_PARAMETER; last error is left alone
 * RT-TRAPS:  core: options other than 0 and REG_OPTION_OPEN_LINK; core: an access mask bit that is none of the documented ones; core: a predefined handle other than the six the tree models; core: a path with two backslashes together; core: a path of backslashes only under HKEY_CLASSES_ROOT; core: a leading backslash below a handle opened under HKEY_CLASSES_ROOT; core: a path beyond a limit
 */
#include "rt/shim.h"
#include "rt/registry.h"

SHIM(advapi32, LSTATUS, RegOpenKeyExA, HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult)
{
    /* The core does all of it (rt_reg_open_key_ex_a, rt/registry.h): the checks of the
     * arguments and the handle, the path, the predefined keys and the access mask, and the
     * result.  The registry functions do not set last error and this shim does not either;
     * the core's handle lookup may set ERROR_INVALID_HANDLE itself, as the header says.  The
     * core stops (fatal, no guard here) for the options, the access mask, a predefined key
     * the tree does not model, a path that NAMES makes fatal, and a path beyond a limit.  A leading
     * or trailing backslash is not fatal here: the core answers it (ERROR_BAD_PATHNAME, or skipped). */
    return rt_reg_open_key_ex_a(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}
