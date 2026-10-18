/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass
 * RT-CASES:  sets each of the six priority classes; a value that is no priority class fails with ERROR_INVALID_PARAMETER and leaves the class unchanged; NULL and an event handle fail with ERROR_INVALID_HANDLE and leave the class unchanged; does not change last error on success
 * RT-TRAPS:  core: PROCESS_MODE_BACKGROUND_BEGIN; core: PROCESS_MODE_BACKGROUND_END
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, BOOL, SetPriorityClass, HANDLE hProcess, DWORD dwPriorityClass)
{
    /* The core does all of it (rt_process_set_priority_class, rt/thread.h): it records the class in
     * the process object, and fails with ERROR_INVALID_HANDLE for a handle that is not an open
     * process handle and with ERROR_INVALID_PARAMETER for a value that is not one of the six
     * priority classes (both set by the core).  Success leaves last error alone.
     *
     * The two PROCESS_MODE_BACKGROUND_ values make the core stop (fatal) for a valid process handle.
     * The shim adds no guard for them.
     *
     * Not checked: the documentation says hProcess must have PROCESS_SET_INFORMATION.  The core does
     * not model access rights on a process handle, and the documentation gives no error code for a
     * handle without that right. */
    return rt_process_set_priority_class(hProcess, dwPriorityClass);
}
