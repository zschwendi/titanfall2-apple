/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fibersapi/nf-fibersapi-flssetvalue
 * RT-CASES:  a value set on an allocated index is read back; FlsSetValue leaves last error alone on success; index 0 and index 128 fail with ERROR_INVALID_PARAMETER; an index in range that was never allocated stores the value and FlsGetValue returns it
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, BOOL, FlsSetValue, DWORD dwFlsIndex, PVOID lpFlsData)
{
    /* The core stores the value in the calling thread's slot and returns TRUE.  An index of 0 or
     * RT_FLS_SLOTS or more fails with FALSE and ERROR_INVALID_PARAMETER, which the core sets; an
     * index in range that is not allocated is stored too (rt/thread.h).  Success leaves last
     * error alone, so the shim sets nothing.  The core makes the thread's FLS array at its first
     * set, so the page's ERROR_NO_MEMORY is not returned. */
    return rt_fls_set(ctx, dwFlsIndex, (uint64_t)(uintptr_t)lpFlsData);
}
