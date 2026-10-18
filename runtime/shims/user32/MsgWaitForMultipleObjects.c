/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjects
 * RT-CASES:  no handle and nobody posts gives WAIT_TIMEOUT after the time limit; a set event gives its index; a posted message gives WAIT_OBJECT_0 + nCount; after the thread has looked at its queue the message is no longer new; a handle that is not open gives WAIT_FAILED with ERROR_INVALID_HANDLE; does not change last error on success
 * RT-TRAPS:  core: a wake mask without QS_POSTMESSAGE; core: fWaitAll with handles; core: more than 63 handles; core: a NULL handle array; core: the same handle twice; core: a second wait after one that returned for the queue with no look between
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, DWORD, MsgWaitForMultipleObjects, DWORD nCount, const HANDLE *pHandles, BOOL fWaitAll, DWORD dwMilliseconds, DWORD dwWakeMask)
{
    /* The runtime's wait with the calling thread's queue as one more object (rt/window.h,
     * rt_msg_wait). */
    return rt_msg_wait(ctx, nCount, pHandles, fWaitAll, dwMilliseconds, dwWakeMask);
}
