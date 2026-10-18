/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode
 * RT-CASES:  returns 0 at first; returns the mode the previous call set; a preset last error stays
 * RT-TRAPS:  core: SEM_NOALIGNMENTFAULTEXCEPT; core: a bit outside the four SEM_ values
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, UINT, SetErrorMode, UINT uMode)
{
    /* The core does all of it (rt_process_swap_error_mode, rt/thread.h): it stores the mode for the
     * process and returns the mode it had (0 at the start).  It cannot fail and never touches last
     * error, so the shim sets nothing.  The documentation calls the call advisory (no error box
     * exists to switch off); the value is kept because callers put back the value they were given.
     *
     * The core stops (fatal) for SEM_NOALIGNMENTFAULTEXCEPT and for a bit outside the four SEM_
     * values.  The shim adds no guard for them. */
    return rt_process_swap_error_mode(uMode);
}
