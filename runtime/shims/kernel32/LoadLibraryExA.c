/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexa
 * RT-CASES:  a native module by its name, in any case, gives its base, and its load count is one higher each time; a name no module has fails with ERROR_MOD_NOT_FOUND; a byte of 0xE9 in the last path component fails with ERROR_MOD_NOT_FOUND, in a directory part it is ignored and the load succeeds; a NULL name fails with ERROR_INVALID_PARAMETER; hFile not NULL fails with ERROR_INVALID_PARAMETER; LOAD_WITH_ALTERED_SEARCH_PATH and the LOAD_LIBRARY_SEARCH flags are accepted and ignored; success leaves last error alone
 * RT-TRAPS:  core: DONT_RESOLVE_DLL_REFERENCES; core: LOAD_LIBRARY_AS_DATAFILE; core: LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE; core: LOAD_LIBRARY_AS_IMAGE_RESOURCE; core: LOAD_IGNORE_CODE_AUTHZ_LEVEL
 */
#include "rt/shim.h"
#include "rt/module.h"

SHIM(kernel32, HMODULE, LoadLibraryExA, LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    RtModule *m;

    /* hFile is reserved and must be NULL. */
    if (hFile) return FAIL(ctx, ERROR_INVALID_PARAMETER, NULL);

    /* The core does the rest (rt_module_load_a, rt/module.h): the flag bits, the name, the
     * look-up and the load; it sets last error itself when it fails.  Five flags make the core
     * stop (rt_unimplemented: DONT_RESOLVE_DLL_REFERENCES, LOAD_LIBRARY_AS_DATAFILE,
     * LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE, LOAD_LIBRARY_AS_IMAGE_RESOURCE,
     * LOAD_IGNORE_CODE_AUTHZ_LEVEL); the shim adds no guard for them and they are listed in
     * RT-TRAPS. */
    m = rt_module_load_a(ctx, lpLibFileName, dwFlags);
    return m ? (HMODULE)(uintptr_t)m->base : NULL;
}
