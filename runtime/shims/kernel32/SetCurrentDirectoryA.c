/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setcurrentdirectory
 * RT-CASES:  a sub-directory is set and read back; a directory that does not exist, and NULL, fail with the code the core gives for the same name; success leaves last error alone
 * RT-TRAPS:  an empty name; a name of MAX_PATH characters or more
 */
#include "rt/shim.h"
#include "rt/path.h"
#include "rt/locale.h"

SHIM(kernel32, BOOL, SetCurrentDirectoryA, LPCSTR lpPathName)
{
    /* The core sets the directory (rt_path_set_cwd_w) and returns its Win32 error, 0 for
     * success; this shim converts the name and passes that code on.  A NULL name goes to the
     * core as it is.  An empty name is refused here: the documentation does not say what it
     * means.  A name that does not fit MAX_PATH with its terminator is refused too. */
    WCHAR wide[MAX_PATH];
    DWORD e;

    if (lpPathName == NULL) {
        e = rt_path_set_cwd_w(NULL);
    } else {
        if (lpPathName[0] == 0) RT_UNIMPLEMENTED(ctx, "SetCurrentDirectoryA with an empty name");
        if (rt_mb_to_wide(CP_ACP, 0, lpPathName, -1, wide, MAX_PATH) == 0)
            RT_UNIMPLEMENTED(ctx, "SetCurrentDirectoryA with a name of MAX_PATH characters or more");
        e = rt_path_set_cwd_w(wide);
    }
    if (e) return FAIL(ctx, e, FALSE);
    return TRUE;
}
