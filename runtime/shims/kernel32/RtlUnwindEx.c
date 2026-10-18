/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnt/nf-winnt-rtlunwindex
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"
#include "rt/win/exception.h"

/* RtlUnwindEx transfers control to TargetFrame and TargetIp.  Its exception record says what
 * kind of transfer it is: code 0x80000026 is a longjmp, 0xE06D7363 a C++ exception (a catch).
 * A transfer of control is deliberately not built (docs/RUNTIME.md section 9.3), so every call
 * is refused.  With ExceptionRecord NULL the code printed is 0. */
SHIM(kernel32, void, RtlUnwindEx, PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD ExceptionRecord, PVOID ReturnValue, PCONTEXT ContextRecord, PUNWIND_HISTORY_TABLE HistoryTable)
{
    (void)ReturnValue;
    (void)ContextRecord;
    (void)HistoryTable;
    RT_UNIMPLEMENTED(ctx, "RtlUnwindEx: a transfer of control (longjmp or a C++ catch) to frame %p, "
                     "address %p, exception code 0x%08x", TargetFrame, TargetIp,
                     (unsigned)(ExceptionRecord ? ExceptionRecord->ExceptionCode : 0));
}
