/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/console/getstdhandle
 * RT-CASES:  the three standard handles are different values, neither NULL nor INVALID_HANDLE_VALUE, and the same on every call; another number gives INVALID_HANDLE_VALUE and ERROR_INVALID_HANDLE; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, HANDLE, GetStdHandle, DWORD nStdHandle)
{
    /* The core does all of it (rt_std_get, rt/file.h): it returns the value stored for
     * STD_INPUT_HANDLE, STD_OUTPUT_HANDLE or STD_ERROR_HANDLE, whatever it is (a value set by
     * SetStdHandle, NULL included), and for any other number INVALID_HANDLE_VALUE with
     * ERROR_INVALID_HANDLE (set by the core).  It makes no new handle, and success leaves
     * last error alone. */
    return rt_std_get(nStdHandle);
}
