/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/console/setstdhandle
 * RT-CASES:  stores a handle, which GetStdHandle then returns; stores NULL and INVALID_HANDLE_VALUE as they are; replaces a handle and closes nothing: the old one stays open; another number fails with ERROR_INVALID_HANDLE and changes nothing; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, SetStdHandle, DWORD nStdHandle, HANDLE hHandle)
{
    /* The core does all of it (rt_std_set, rt/file.h): it stores hHandle as it is, NULL and
     * invalid values included, and closes nothing (the value it replaces stays open).  For a
     * number other than STD_INPUT_HANDLE, STD_OUTPUT_HANDLE or STD_ERROR_HANDLE it returns FALSE
     * with ERROR_INVALID_HANDLE (set by the core).  Success returns TRUE and leaves last error
     * alone. */
    return rt_std_set(nStdHandle, hHandle);
}
