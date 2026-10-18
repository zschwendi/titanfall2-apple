/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fibersapi/nf-fibersapi-flsfree
 * RT-CASES:  index 0, index 128 and an index that is not allocated fail with ERROR_INVALID_PARAMETER; a nonzero value runs the callback once, with the value; a zero value does not run the callback; an index allocated with a NULL callback is freed without a call; a preset last error is left alone on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, BOOL, FlsFree, DWORD dwFlsIndex)
{
    /* The core frees the index, zeroes the values, and calls each non-NULL callback for a
     * nonzero value on the calling thread (rt/thread.h); on failure it sets
     * ERROR_INVALID_PARAMETER and returns FALSE.  Success leaves last error alone, so the shim
     * sets nothing. */
    return rt_fls_free(ctx, dwFlsIndex);
}
