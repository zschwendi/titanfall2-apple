/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw
 * RT-CASES:  by name, with UNCHANGED_REFCOUNT, gives the module and leaves its load count alone; by name with flags 0 raises the load count by one; a name without the extension, or in capitals, is found; the NULL name gives the executable; by address, the module whose range holds the address is found; a NULL address with the address flag fails with ERROR_MOD_NOT_FOUND; a name no module has fails with ERROR_MOD_NOT_FOUND and sets *phModule to NULL; PIN with UNCHANGED_REFCOUNT, a flag bit of 8 and phModule NULL fail with ERROR_INVALID_PARAMETER; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, BOOL, GetModuleHandleExW, DWORD dwFlags, LPCVOID lpModuleName, HMODULE *phModule)
{
    /* The core does all of it (rt_module_handle_ex, rt/module.h): it checks the flags, finds
     * the module by name or by address, counts the reference, and fails with ERROR_MOD_NOT_FOUND
     * or ERROR_INVALID_PARAMETER (set by the core) on the failures the documentation describes
     * without naming the codes.  Success leaves last error alone.  phModule is written in
     * every case, NULL on failure, as the documentation says. */
    RtModule *m = NULL;
    BOOL ok = rt_module_handle_ex(dwFlags, lpModuleName, true, phModule ? &m : NULL);
    if (phModule) *phModule = m ? (HMODULE)(uintptr_t)m->base : NULL;
    return ok;
}
