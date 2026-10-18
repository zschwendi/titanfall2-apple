/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/interlockedapi/nf-interlockedapi-interlockedflushslist
 * RT-CASES:  returns NULL and leaves an empty list alone; returns the first entry and leaves the list empty; the entries after the first stay chained; the depth reads 0 afterwards; does not change last error
 * RT-TRAPS:  NULL list header
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, PSLIST_ENTRY, InterlockedFlushSList, PSLIST_HEADER ListHead)
{
    if (!ListHead) RT_UNIMPLEMENTED(ctx, "NULL list header (Windows raises an access violation; not modelled)");
    return rt_slist_flush(ListHead);
}
