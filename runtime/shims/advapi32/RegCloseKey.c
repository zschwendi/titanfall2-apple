/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winreg/nf-winreg-regclosekey
 * RT-CASES:  closes an open key, so the next close of it fails with ERROR_INVALID_HANDLE; a predefined key closes with ERROR_SUCCESS and nothing happens; NULL and a handle that is not open fail with ERROR_INVALID_HANDLE; a handle of another kind (an event) fails with ERROR_INVALID_HANDLE; a handle to a deleted key can be closed; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/registry.h"

SHIM(advapi32, LSTATUS, RegCloseKey, HKEY hKey)
{
    /* The core does all of it (rt_reg_close_key, rt/registry.h): a predefined key is not in the
     * handle table and closing it succeeds with no effect; an open key handle is closed, and
     * a handle to a deleted key can be closed too; NULL, a handle that is not open and a handle
     * of another kind fail with ERROR_INVALID_HANDLE.  The registry functions do not set last
     * error and this shim does not either; the core's handle lookup may set ERROR_INVALID_HANDLE
     * itself, as the header says. */
    return rt_reg_close_key(hKey);
}
