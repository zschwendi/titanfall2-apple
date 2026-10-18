/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate
 * RT-CASES:  a first call with ES_CONTINUOUS | ES_DISPLAY_REQUIRED returns ES_CONTINUOUS; a second call returns the flags of the first; a call without ES_CONTINUOUS returns the state and leaves it as it is; ES_USER_PRESENT with another flag fails with ERROR_INVALID_PARAMETER; does not change last error on success
 * RT-TRAPS:  core: ES_USER_PRESENT alone; core: ES_AWAYMODE_REQUIRED without ES_CONTINUOUS; core: a bit outside the five ES_ values
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, EXECUTION_STATE, SetThreadExecutionState, EXECUTION_STATE esFlags)
{
    /* The core does all of it (rt_thread_execution_state, rt/thread.h): the state is kept per
     * thread, a call with ES_CONTINUOUS replaces it, a call without leaves it, and the state
     * before the call is returned; ES_USER_PRESENT with another flag fails with 0 and
     * ERROR_INVALID_PARAMETER (set by the core).  The host is told nothing.  Success leaves last
     * error alone.
     *
     * The core stops (fatal) on what the page leaves open: ES_USER_PRESENT alone,
     * ES_AWAYMODE_REQUIRED without ES_CONTINUOUS, and any bit outside the five ES_ values.  The
     * shim adds no guard for them. */
    return rt_thread_execution_state(ctx, esFlags);
}
