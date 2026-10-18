/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/shlobj_core/nf-shlobj_core-shgetspecialfolderpatha
 * RT-CASES:  returns TRUE and the text of each of the six folders, without a trailing backslash; CSIDL_PERSONAL with CSIDL_FLAG_CREATE gives the Documents text; fCreate TRUE and FALSE give the same text; every byte after the terminator stays; does not change last error
 * RT-TRAPS:  NULL pszPath; another folder
 */
#include "rt/shim.h"
#include "rt/path.h"
#include "rt/win/shell.h"

SHIM(shell32, BOOL, SHGetSpecialFolderPathA, HWND hwnd, LPSTR pszPath, int csidl, BOOL fCreate)
{
    RtKnownPath which;

    (void)hwnd;                         /* reserved by the documentation */
    (void)fCreate;                      /* the folders exist: nothing is created */
    if (!pszPath)
        RT_UNIMPLEMENTED(ctx, "SHGetSpecialFolderPathA with a NULL pszPath buffer");
    switch (csidl & ~CSIDL_FLAG_CREATE) {
    case CSIDL_PERSONAL:      which = RT_KNOWN_DOCUMENTS; break;
    case CSIDL_LOCAL_APPDATA: which = RT_KNOWN_LOCAL_APPDATA; break;
    case CSIDL_APPDATA:       which = RT_KNOWN_APPDATA; break;
    case CSIDL_PROFILE:       which = RT_KNOWN_PROFILE; break;
    case CSIDL_WINDOWS:       which = RT_KNOWN_WINDOWS; break;
    case CSIDL_SYSTEM:        which = RT_KNOWN_SYSTEM; break;
    default:
        RT_UNIMPLEMENTED(ctx, "SHGetSpecialFolderPathA with another folder, CSIDL 0x%x", (unsigned)csidl);
    }
    /* MAX_PATH holds every text, so the core always writes it (the page: a buffer of MAX_PATH). */
    rt_path_known_a(which, pszPath, MAX_PATH);
    return TRUE;
}
