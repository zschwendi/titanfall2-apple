/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlew
 * RT-CASES:  NULL gives the executable; kernel32.dll, KERNEL32, kernel32 and a system path to kernel32.dll give kernel32's base; a name no module has gives NULL and ERROR_MOD_NOT_FOUND; success leaves last error alone
 * RT-TRAPS:  a character outside ASCII in the name
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, HMODULE, GetModuleHandleW, LPCWSTR lpModuleName)
{
    /* The page does not say how a letter outside ASCII compares with a module name, so such a
     * name is refused here.  The name's path is the core's to read (rt_module_find_w takes the
     * last component): a shim must not parse paths. */
    if (lpModuleName) {
        for (const WCHAR *p = lpModuleName; *p; p++) {
            if (*p >= 0x80) RT_UNIMPLEMENTED(ctx, "GetModuleHandleW with a character outside ASCII in the name %p", (const void *)lpModuleName);
        }
    }
    RtModule *m = rt_module_handle_w(lpModuleName);
    return m ? (HMODULE)(uintptr_t)m->base : NULL;
}
