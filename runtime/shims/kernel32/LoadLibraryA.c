/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibrarya
 * RT-CASES:  a native module by its name, in any case, gives its base, and its load count is one higher each time; a name no module has fails with ERROR_MOD_NOT_FOUND; a byte of 0xE9 in the last path component fails with ERROR_MOD_NOT_FOUND, in a directory part it is ignored and the load succeeds; a NULL name fails with ERROR_INVALID_PARAMETER; success leaves last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, HMODULE, LoadLibraryA, LPCSTR lpLibFileName)
{
    /* LoadLibraryA is the core's rt_module_load_a with no flags.  The core does the name look-up
     * and the load, and sets last error itself when it fails. */
    RtModule *m = rt_module_load_a(ctx, lpLibFileName, 0);
    return m ? (HMODULE)(uintptr_t)m->base : NULL;
}
