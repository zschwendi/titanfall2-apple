/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setcurrentdirectory
 * RT-CASES:  sets an existing directory and returns non-zero; setting .. goes back up; a directory that does not exist fails with the core's code, the directory stays; a file fails with the core's code, the directory stays; NULL fails with the core's code, the directory stays; success does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/path.h"

SHIM(kernel32, BOOL, SetCurrentDirectoryW, LPCWSTR lpPathName)
{
    DWORD e = rt_path_set_cwd_w(lpPathName);
    if (e) return FAIL(ctx, e, FALSE);
    return TRUE;
}
