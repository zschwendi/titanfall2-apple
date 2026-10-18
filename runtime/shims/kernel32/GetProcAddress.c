/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress
 * RT-CASES:  a name the module exports gives the core's address (base + RVA); a name the module does not have, or spelled in another case, gives NULL with ERROR_PROC_NOT_FOUND; FlsAlloc, a name the C runtime probes for and kernel32 does not have, gives NULL with ERROR_PROC_NOT_FOUND; an ordinal in the low word is answered as the core answers it; a NULL name is ordinal 0, which the core answers; a module handle that is no module gives NULL with ERROR_MOD_NOT_FOUND; the NULL handle is the executable, as the core answers it; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, FARPROC, GetProcAddress, HMODULE hModule, LPCVOID lpProcName)
{
    /* The core does all of it (rt_module_from_handle and rt_module_export, rt/module.h): the
     * module, the lookup by name or by ordinal, the miss (NULL with ERROR_PROC_NOT_FOUND and
     * one import log line) and the bad handle (NULL with ERROR_MOD_NOT_FOUND, set by the core).
     * Success leaves last error alone.  The shim sets nothing.  On a stubbed game DLL the
     * core has no miss: a look-up that finds no shim stops the run there (rt/module.h).
     *
     * lpProcName is a name or an ordinal: a value below 0x10000 is the ordinal (the high-order
     * word must be zero, as the documentation says), anything else is a string. */
    RtModule *m = rt_module_from_handle(hModule);
    uintptr_t v;

    if (!m) return 0;
    v = (uintptr_t)lpProcName;
    if (v < 0x10000) return rt_module_export(m, NULL, (uint32_t)v);
    return rt_module_export(m, (const char *)lpProcName, 0);
}
