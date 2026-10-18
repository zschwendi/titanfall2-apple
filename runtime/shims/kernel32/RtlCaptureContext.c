/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnt/nf-winnt-rtlcapturecontext
 * RT-CASES:  Rip and Rsp are set for the caller and Rcx is the ContextRecord pointer; EFlags, segments, ContextFlags, MxCsr and the save area are written as the core specifies, and the fields it does not write keep the caller's bytes; does not change last error
 * RT-TRAPS:  NULL ContextRecord
 */
#include "rt/shim.h"
#include "rt/unwind.h"

SHIM(kernel32, void, RtlCaptureContext, PCONTEXT ContextRecord)
{
    /* The documentation gives no result for a NULL ContextRecord (Windows faults on it), so
     * it is not guessed. */
    if (!ContextRecord) RT_UNIMPLEMENTED(ctx, "RtlCaptureContext with NULL ContextRecord");

    /* The core reads the return address at ctx->r[4], which is why this call stays in the
     * shim body (rt/unwind.h).  Last error is never touched. */
    rt_unwind_capture(ctx, ContextRecord);
}
