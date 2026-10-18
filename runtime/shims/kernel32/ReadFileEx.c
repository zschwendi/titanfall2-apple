/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-readfileex
 * RT-CASES:  overlapped read at an offset; read at the end of the file; a handle that is not open fails with ERROR_INVALID_HANDLE; a handle opened for writing only fails with ERROR_ACCESS_DENIED
 * RT-TRAPS:  core: a handle without FILE_FLAG_OVERLAPPED; core: lpOverlapped NULL; core: lpCompletionRoutine 0
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, ReadFileEx, HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPOVERLAPPED lpOverlapped, LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    /* The core does all of it (rt_file_read_ex, rt/file.h): it reads the bytes, sets the
     * OVERLAPPED's Internal and InternalHigh, queues the completion routine to the calling
     * thread as an APC, and sets last error itself (0 on success, as ReadFileEx documents).
     * Its stops (a handle without FILE_FLAG_OVERLAPPED, lpOverlapped NULL, a completion
     * routine 0) are the core's own; the shim adds no guard for them (listed in RT-TRAPS). */
    return rt_file_read_ex(ctx, hFile, lpBuffer, nNumberOfBytesToRead, lpOverlapped, lpCompletionRoutine);
}
