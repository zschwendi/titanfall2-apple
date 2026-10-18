/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult
 * RT-CASES:  completed read: TRUE and the byte count, with bWait FALSE or TRUE; end of file: FALSE, 0 bytes, ERROR_HANDLE_EOF; pending with bWait FALSE: FALSE, ERROR_IO_INCOMPLETE; a handle that is not open fails with ERROR_INVALID_HANDLE; does not change last error on success
 * RT-TRAPS:  core: STATUS_PENDING with bWait TRUE; core: lpOverlapped NULL; core: lpNumberOfBytesTransferred NULL; core: any other Internal status
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, GetOverlappedResult, HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait)
{
    /* The core does all of it (rt_file_overlapped_result, rt/file.h): it reads the result out
     * of lpOverlapped, checks the handle, and sets last error itself on failure.  Its stops (a
     * pending result with bWait TRUE, a NULL lpOverlapped or lpNumberOfBytesTransferred, and
     * any other status) are the core's own; the shim adds no guard for them (all four are
     * listed in RT-TRAPS). */
    return rt_file_overlapped_result(hFile, lpOverlapped, lpNumberOfBytesTransferred, bWait);
}
