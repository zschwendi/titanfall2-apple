/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnt/nf-winnt-rtlvirtualunwind
 * RT-CASES:  a function with no frame register and no allocation: Rip is the return address, Rsp moves past it, the establisher frame is the Rsp before the step, and the result is NULL; the rest of the context and the handler data are not written; HandlerType 0, 1 and 2 are all accepted; ContextPointers may be NULL, and when given none of its entries is written; does not change last error
 * RT-TRAPS:  NULL FunctionEntry; NULL ContextRecord; NULL HandlerData; NULL EstablisherFrame; HandlerType above 2
 */
#include "rt/shim.h"
#include "rt/unwind.h"

SHIM(kernel32, PEXCEPTION_ROUTINE, RtlVirtualUnwind, DWORD HandlerType, DWORD64 ImageBase, DWORD64 ControlPc, PRUNTIME_FUNCTION FunctionEntry, PCONTEXT ContextRecord, PVOID *HandlerData, PDWORD64 EstablisherFrame, PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    /* Windows dereferences the four pointers below, so NULL faults there; a fault is not
     * modelled here.  HandlerType above 2 is not one of the three UNW_FLAG_* handler types the
     * core takes.  The documentation does not mention last error, and the core never touches
     * it, so the shim does not either. */
    if (!FunctionEntry) RT_UNIMPLEMENTED(ctx, "NULL FunctionEntry (Windows faults on it; not modelled): %p", (void *)FunctionEntry);
    if (!ContextRecord) RT_UNIMPLEMENTED(ctx, "NULL ContextRecord (Windows faults on it; not modelled): %p", (void *)ContextRecord);
    if (!HandlerData) RT_UNIMPLEMENTED(ctx, "NULL HandlerData (Windows faults on it; not modelled): %p", (void *)HandlerData);
    if (!EstablisherFrame) RT_UNIMPLEMENTED(ctx, "NULL EstablisherFrame (Windows faults on it; not modelled): %p", (void *)EstablisherFrame);
    if (HandlerType > 2) RT_UNIMPLEMENTED(ctx, "HandlerType above 2: %#x", HandlerType);
    return rt_unwind_step(HandlerType, ImageBase, ControlPc, FunctionEntry, ContextRecord,
                          (uint64_t *)HandlerData, EstablisherFrame, ContextPointers);
}
