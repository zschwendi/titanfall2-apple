/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnt/nf-winnt-rtllookupfunctionentry
 * RT-CASES:  an address in no module gives NULL and sets *ImageBase to 0; a native module's address gives NULL and sets *ImageBase to that module's base; a non-NULL HistoryTable changes nothing; does not change last error
 * RT-TRAPS:  NULL ImageBase
 */
#include "rt/shim.h"
#include "rt/unwind.h"

SHIM(kernel32, PRUNTIME_FUNCTION, RtlLookupFunctionEntry, DWORD64 ControlPc, PDWORD64 ImageBase, PUNWIND_HISTORY_TABLE HistoryTable)
{
    /* Windows writes the module's base through ImageBase, so a NULL ImageBase is not handled
     * (RT-TRAPS). */
    if (!ImageBase) RT_UNIMPLEMENTED(ctx, "RtlLookupFunctionEntry with NULL ImageBase");

    /* HistoryTable is a cache for the caller's benefit; the core has no counterpart for it, so
     * it is ignored: the result does not depend on it. */
    (void)HistoryTable;

    /* The core does the whole look-up (rt_unwind_lookup, rt/unwind.h): it takes the loader's
     * lock itself, sets no last error, and writes *ImageBase on every path.  The shim holds no
     * lock and does not touch last error.
     *
     * Success (a function entry of a lifted module with unwind data) needs such a module, which
     * a shim test does not have: that case is the core's test, core/unwind_corpus
     * (runtime/tests/core/unwind.c). */
    return (PRUNTIME_FUNCTION)rt_unwind_lookup(ControlPc, ImageBase);
}
