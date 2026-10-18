/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-freelibrary
 * RT-CASES:  frees a loaded module, so its load count goes down by one and TRUE is returned; does not change last error on success; a handle that is no module fails with ERROR_MOD_NOT_FOUND
 * RT-TRAPS:  FreeLibrary(NULL)
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, BOOL, FreeLibrary, HMODULE hLibModule)
{
    RtModule *m;

    /* What Windows does with a NULL handle here is not verified (the documentation does not
     * say), so it is not guessed. */
    if (!hLibModule) RT_UNIMPLEMENTED(ctx, "FreeLibrary(NULL)");
    /* A handle that is no module: the core fails with ERROR_MOD_NOT_FOUND and sets it. */
    m = rt_module_from_handle(hLibModule);
    if (!m) return FALSE;
    /* The core lowers the load count, logs, and returns TRUE; it sets nothing. */
    return rt_module_free(m);
}
