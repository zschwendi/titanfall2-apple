/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-openfilemappinga
 * RT-CASES:  a name of a section that exists gives a handle and leaves last error alone; a name nobody made fails with ERROR_FILE_NOT_FOUND; a name of MAX_PATH - 1 characters is looked up and one of MAX_PATH characters or more fails with ERROR_FILENAME_EXCED_RANGE
 * RT-TRAPS:  a NULL name; core: an empty name; core: a backslash in the name
 */
#include "rt/shim.h"
#include "rt/file.h"
#include "rt/locale.h"

SHIM(kernel32, HANDLE, OpenFileMappingA, DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)
{
    /* The core opens the named section (rt_section_open, rt/file.h): it sets last error when it
     * fails (ERROR_FILE_NOT_FOUND, ERROR_INVALID_HANDLE for a name of another kind, and the rest
     * of its list) and leaves it alone on success.  This shim leaves it alone too, and converts
     * the name.
     *
     * dwDesiredAccess is not used: the access mask is not taken, as the core's rule for a
     * handle's rights says.  bInheritHandle is not used either, because this runtime never
     * creates a child process and so nothing can inherit the handle (planner's decision,
     * 2026-10-09).  Neither is a trap.
     *
     * A NULL name is refused here, as RT-TRAPS says: the documentation does not say what Windows
     * does with one, and the core's ERROR_INVALID_PARAMETER for it is from memory (rt_object_open,
     * core/handle.c).  The core stops on an empty name and on a backslash in the name (RT-TRAPS,
     * core: ...); the shim adds no guard for those.  A name of MAX_PATH characters or more is
     * refused here with ERROR_FILENAME_EXCED_RANGE, as CreateFileMappingA refuses it. */
    WCHAR wide[MAX_PATH];
    int n;

    (void)dwDesiredAccess;
    (void)bInheritHandle;
    if (lpName == NULL) RT_UNIMPLEMENTED(ctx, "OpenFileMappingA with a NULL name");

    /* The conversion writes the name and its terminator: a name that does not fit is result 0. */
    n = rt_mb_to_wide(CP_ACP, 0, lpName, -1, wide, MAX_PATH);
    if (n == 0) return FAIL(ctx, ERROR_FILENAME_EXCED_RANGE, NULL);
    return rt_section_open(wide);
}
