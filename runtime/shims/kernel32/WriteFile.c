/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-writefile
 * RT-CASES:  synchronous write to a file; bytes written through one handle are read back; zero-length write does not truncate or extend the file; a write beyond the end extends the file; a handle that is not open fails with ERROR_INVALID_HANDLE; a handle opened for reading only fails with ERROR_ACCESS_DENIED; the count is set to 0 before a failure; does not change last error on success
 * RT-TRAPS:  core: both lpNumberOfBytes and lpOverlapped NULL; core: a directory handle
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, WriteFile, HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    /* The core does all of it (rt_file_write, rt/file.h): it sets the count to 0 first, checks
     * the handle, the access and the length (zero: no host call), makes the host writes and
     * maps their failures, and sets last error itself on failure.  lpOverlapped is passed on as
     * it came: the core takes the OVERLAPPED path (rt_file_ov_write).  Its two stops, a call
     * with both lpNumberOfBytesWritten and lpOverlapped NULL and a write on a directory handle,
     * are the core's own; the shim adds no guard for them (listed in RT-TRAPS). */
    return rt_file_write(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}
