/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-initializeconditionvariable
 * RT-CASES:  the variable is set to its initial state, all zero; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, InitializeConditionVariable, PCONDITION_VARIABLE ConditionVariable)
{
    rt_cv_init(ConditionVariable);
}
