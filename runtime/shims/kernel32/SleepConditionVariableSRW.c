/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-sleepconditionvariablesrw
 * RT-CASES:  a timeout returns FALSE with ERROR_TIMEOUT, exclusive lock; a timeout returns FALSE with ERROR_TIMEOUT, shared lock; a zero timeout returns at once with ERROR_TIMEOUT; the lock is held again in the mode it was held before
 * RT-TRAPS:  Flags other than 0 and CONDITION_VARIABLE_LOCKMODE_SHARED
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, BOOL, SleepConditionVariableSRW, PCONDITION_VARIABLE ConditionVariable, PSRWLOCK SRWLock, DWORD dwMilliseconds, ULONG Flags)
{
    if (Flags & ~(ULONG)CONDITION_VARIABLE_LOCKMODE_SHARED)
        RT_UNIMPLEMENTED(ctx, "SleepConditionVariableSRW with Flags other than 0 and CONDITION_VARIABLE_LOCKMODE_SHARED (Flags is %#x)", Flags);
    return rt_cv_sleep_srw(ctx, ConditionVariable, SRWLock, dwMilliseconds, Flags);
}
