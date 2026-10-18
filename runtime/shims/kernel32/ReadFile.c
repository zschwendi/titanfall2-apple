/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-readfile
 * RT-CASES:  synchronous read from a file; zero-length read; short read at the end of the file; end of file; a handle that is not open fails with ERROR_INVALID_HANDLE; a handle opened for writing only fails with ERROR_ACCESS_DENIED; the count is set to 0 before a failure; does not change last error on success
 * RT-TRAPS:  core: both lpNumberOfBytes and lpOverlapped NULL; core: a directory handle
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, ReadFile, HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    /* The core does all of it (rt_file_read, rt/file.h): it sets the count to 0 first, checks
     * the handle, the access and the length, makes the host read and maps its failures, and
     * sets last error itself on failure.  lpOverlapped is passed on as it came: the core takes
     * the OVERLAPPED path (rt_file_ov_read).  Its two stops, a call with both lpNumberOfBytesRead
     * and lpOverlapped NULL and a read on a directory handle, are the core's own; the shim adds
     * no guard for them (listed in RT-TRAPS). */
    return rt_file_read(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}
