/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-wakeconditionvariable
 * RT-CASES:  with no thread sleeping it has no effect, so a sleep after it still times out; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, WakeConditionVariable, PCONDITION_VARIABLE ConditionVariable)
{
    rt_cv_wake(ConditionVariable);
}
