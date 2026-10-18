/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-createfilemappinga
 * RT-CASES:  a section with no file and no name gives a handle and ERROR_SUCCESS; a first create with a name gives ERROR_SUCCESS and a second create with the same name a handle and ERROR_ALREADY_EXISTS; a name of MAX_PATH - 1 characters is accepted and one of MAX_PATH characters or more fails with ERROR_FILENAME_EXCED_RANGE; no file and a size of 0 fails with ERROR_INVALID_PARAMETER; a closed handle given as the file fails with ERROR_INVALID_HANDLE; a name of another kind of object fails with ERROR_INVALID_HANDLE; the security attributes pointer, NULL or not, has no effect
 * RT-TRAPS:  core: a NULL file handle; core: a protection other than the three; core: an empty name; core: a backslash in the name
 */
#include "rt/shim.h"
#include "rt/file.h"
#include "rt/locale.h"

SHIM(kernel32, HANDLE, CreateFileMappingA, HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName)
{
    /* The core does the section (rt_section_create, rt/file.h): it checks the file, the protection
     * and the size, makes the section or opens the named one, and sets last error itself
     * (ERROR_SUCCESS, or ERROR_ALREADY_EXISTS for a name that existed; a failure's code).  This
     * shim converts the name and passes the rest on, unchanged.
     *
     * lpFileMappingAttributes is not used, NULL or not.  Its security descriptor is not modelled,
     * so the section gets the default one; that is not a trap.  Its bInheritHandle has no effect,
     * because this runtime never creates a child process and so nothing can inherit the handle
     * (planner's decision, 2026-10-09).
     *
     * The core's stops are not guarded here (RT-TRAPS, core: ...): a NULL file handle, a
     * protection other than the three, an empty name and a backslash in the name.  The shim
     * refuses only a name that does not fit MAX_PATH: a name of MAX_PATH characters or more gives
     * a result of 0 from the conversion, and fails with ERROR_FILENAME_EXCED_RANGE before the core
     * sees it.  A NULL name is an unnamed section, passed to the core as NULL. */
    ULONGLONG max_size = ((ULONGLONG)dwMaximumSizeHigh << 32) | dwMaximumSizeLow;
    WCHAR wide[MAX_PATH];
    int n;

    (void)lpFileMappingAttributes;
    if (lpName == NULL) return rt_section_create(hFile, flProtect, max_size, NULL);

    /* The conversion writes the name and its terminator: a name that does not fit is result 0. */
    n = rt_mb_to_wide(CP_ACP, 0, lpName, -1, wide, MAX_PATH);
    if (n == 0) return FAIL(ctx, ERROR_FILENAME_EXCED_RANGE, NULL);
    return rt_section_create(hFile, flProtect, max_size, wide);
}
