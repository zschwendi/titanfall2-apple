/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryw
 * RT-CASES:  loads a native module by its file name and counts the load; success leaves last error alone; a name no module has fails with ERROR_MOD_NOT_FOUND; a NULL or empty name fails with ERROR_INVALID_PARAMETER
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, HMODULE, LoadLibraryW, LPCWSTR lpLibFileName)
{
    /* LoadLibraryExW with flags 0 (rt_module_load, rt/module.h).  With no flags no stop of the
     * core can be reached.  The core sets last error itself when it fails and leaves it alone on
     * success; this shim sets nothing. */
    RtModule *m = rt_module_load(ctx, lpLibFileName, 0);
    return m ? (HMODULE)(uintptr_t)m->base : NULL;
}
