/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnt/nf-winnt-rtlpctofileheader
 * RT-CASES:  the base address of kernel32 gives that base, stored and returned; an address inside a module, not at its base, gives that module's base; an address in no module gives NULL, stored and returned; does not change last error, on success or on failure
 * RT-TRAPS:  BaseOfImage NULL
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, PVOID, RtlPcToFileHeader, PVOID PcValue, PVOID *BaseOfImage)
{
    /* The documentation does not say what a NULL BaseOfImage means, so it is not guessed. */
    if (!BaseOfImage) RT_UNIMPLEMENTED(ctx, "RtlPcToFileHeader with BaseOfImage NULL");

    /* The core finds the loaded module whose range contains the PC value, native modules
     * included, and never sets last error (rt/module.h).  No module gives NULL, which is
     * stored in *BaseOfImage as well as returned. */
    RtModule *m = rt_module_from_pc((uint64_t)(uintptr_t)PcValue);
    PVOID base = m ? (PVOID)(uintptr_t)m->base : NULL;
    *BaseOfImage = base;
    return base;
}
