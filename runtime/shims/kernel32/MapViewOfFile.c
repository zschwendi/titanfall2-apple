/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile
 * RT-CASES:  a read-write view of the whole section takes a write and a second view shows it; FILE_MAP_READ of a file shows its bytes; bytes past the end of the file in its last page read as zeros; an offset of 65536 with size 4096 maps that part of the section; an offset that is not a multiple of 64 KB fails with ERROR_MAPPED_ALIGNMENT; a handle that is no section fails with ERROR_INVALID_HANDLE; a read-write view of a read-only section fails with ERROR_ACCESS_DENIED; an offset at or beyond the section's size fails with ERROR_ACCESS_DENIED; a view that ends beyond the section's size rounded up to 4 KB fails with ERROR_ACCESS_DENIED; a copy-on-write view takes a write and neither the file nor another view changes; the view still works after its section and file handles are closed; success leaves last error alone
 * RT-TRAPS:  core: an access that is none of the listed forms; core: a view that ends beyond the section's size inside its last 4 KB page
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, LPVOID, MapViewOfFile, HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap)
{
    /* The core does every check, in the documented order, and sets last error itself when it
     * fails (rt_view_map, rt/file.h).  This shim only joins the 64-bit offset from its two halves
     * and passes the rest on, unchanged.
     *
     * The core's stops are not guarded here (RT-TRAPS, core: ...): an access that is none of the
     * listed forms, and a view that ends beyond the section's size inside its last 4 KB page. */
    return (LPVOID)(uintptr_t)rt_view_map(hFileMappingObject, dwDesiredAccess,
                                          (((ULONGLONG)dwFileOffsetHigh) << 32) | dwFileOffsetLow,
                                          dwNumberOfBytesToMap);
}
