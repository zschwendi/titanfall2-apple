/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlea
 * RT-CASES:  NULL gives the executable; kernel32.dll, KERNEL32, kernel32 and a system path to kernel32.dll give kernel32's base; a name no module has gives NULL and ERROR_MOD_NOT_FOUND; success leaves last error alone
 * RT-TRAPS:  a character outside ASCII in the name
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, HMODULE, GetModuleHandleA, LPCSTR lpModuleName)
{
    /* The page does not say how a letter outside ASCII (a byte of the ANSI code page) compares
     * with a module name, so such a name is refused here.  The name's path is the core's to read
     * (rt_module_find takes the last component): a shim must not parse paths. */
    if (lpModuleName) {
        for (const char *p = lpModuleName; *p; p++) {
            if ((unsigned char)*p >= 0x80) RT_UNIMPLEMENTED(ctx, "GetModuleHandleA with a character outside ASCII in the name %p", (const void *)lpModuleName);
        }
    }
    RtModule *m = rt_module_handle_a(lpModuleName);
    return m ? (HMODULE)(uintptr_t)m->base : NULL;
}
