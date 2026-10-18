/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamea
 * RT-CASES:  the executable for a NULL module; kernel32 by its handle, with its name and its length; a buffer one character too small returns nSize, is terminated and sets ERROR_INSUFFICIENT_BUFFER; nSize 0 returns 0, sets ERROR_INSUFFICIENT_BUFFER and writes nothing; a handle that is no module fails with ERROR_MOD_NOT_FOUND; success does not change last error
 * RT-TRAPS:  core: lpFilename NULL with nSize not 0
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, DWORD, GetModuleFileNameA, HMODULE hModule, LPSTR lpFilename, DWORD nSize)
{
    /* The core does all of it (rt/module.h): it names the module, copies at most nSize
     * characters, and returns the length or nSize with ERROR_INSUFFICIENT_BUFFER when the name
     * does not fit.  rt_module_from_handle sets ERROR_MOD_NOT_FOUND for a handle that is no
     * module, so the shim returns 0 and sets nothing.  A NULL lpFilename with nSize not 0
     * STOPS in the core: no guard here. */
    RtModule *m = rt_module_from_handle(hModule);
    if (!m) return 0;
    return rt_module_filename_a(m, lpFilename, nSize);
}
