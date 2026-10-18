/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getdiskfreespaceexa
 * RT-CASES:  C root gives the core total, and free is not above total; NULL root is the root of the current disk and gives the same total as C root; a directory of the install root gives the total of the volume it lies on; a pointer passed as NULL is not written and the others are; all three pointers NULL is TRUE; D root, empty root and a missing directory fail with ERROR_PATH_NOT_FOUND and write nothing; success leaves a preset last error alone
 * RT-TRAPS:  core: a name that is not a directory
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, GetDiskFreeSpaceExA, LPCSTR lpDirectoryName, PULARGE_INTEGER lpFreeBytesAvailableToCaller, PULARGE_INTEGER lpTotalNumberOfBytes, PULARGE_INTEGER lpTotalNumberOfFreeBytes)
{
    /* Each out-pointer the guest gives is handed to the core as the address of its QuadPart;
     * a NULL pointer stays NULL.  The name goes to the core as it came. */
    ULONGLONG *avail = lpFreeBytesAvailableToCaller ? &lpFreeBytesAvailableToCaller->QuadPart : NULL;
    ULONGLONG *total = lpTotalNumberOfBytes ? &lpTotalNumberOfBytes->QuadPart : NULL;
    ULONGLONG *free_ = lpTotalNumberOfFreeBytes ? &lpTotalNumberOfFreeBytes->QuadPart : NULL;
    return rt_disk_space(RT_NAME_A(lpDirectoryName), avail, total, free_);
}
