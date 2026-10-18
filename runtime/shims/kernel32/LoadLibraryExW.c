/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw
 * RT-CASES:  loads a native module by its file name and counts the load; success leaves last error alone; a name no module has fails with ERROR_MOD_NOT_FOUND; a NULL or empty name fails with ERROR_INVALID_PARAMETER, whatever the flags; a flag bit of 0x80000000 fails with ERROR_INVALID_PARAMETER; LOAD_WITH_ALTERED_SEARCH_PATH is accepted; hFile not NULL fails with ERROR_INVALID_PARAMETER
 * RT-TRAPS:  core: LOAD_LIBRARY_AS_DATAFILE; core: LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE; core: LOAD_LIBRARY_AS_IMAGE_RESOURCE; core: DONT_RESOLVE_DLL_REFERENCES; core: LOAD_IGNORE_CODE_AUTHZ_LEVEL
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, HMODULE, LoadLibraryExW, LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    /* The core does the load (rt_module_load, rt/module.h): the flag and name checks, the
     * registry, the loader lock, and for a lifted module the whole load.  It sets last error
     * itself when it fails and leaves it alone on success.  The documentation says hFile is
     * reserved and must be NULL; the code for anything else is the loader contract's choice.
     *
     * The core stops (RT_UNIMPLEMENTED, with the flag and the name) for DONT_RESOLVE_DLL_REFERENCES,
     * LOAD_LIBRARY_AS_DATAFILE, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE, LOAD_LIBRARY_AS_IMAGE_RESOURCE
     * and LOAD_IGNORE_CODE_AUTHZ_LEVEL.  This shim adds no guard for them. */
    RtModule *m;

    if (hFile != NULL) return FAIL(ctx, ERROR_INVALID_PARAMETER, NULL);
    m = rt_module_load(ctx, lpLibFileName, dwFlags);
    return m ? (HMODULE)(uintptr_t)m->base : NULL;
}
