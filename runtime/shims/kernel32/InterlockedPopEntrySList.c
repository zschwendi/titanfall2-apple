/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/interlockedapi/nf-interlockedapi-interlockedpopentryslist
 * RT-CASES:  returns NULL and leaves the header alone for an empty list; returns the first entry and removes it; the depth in the low 16 bits counts down; does not change last error
 * RT-TRAPS:  NULL list header
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, PSLIST_ENTRY, InterlockedPopEntrySList, PSLIST_HEADER ListHead)
{
    if (!ListHead) RT_UNIMPLEMENTED(ctx, "NULL list header (Windows raises an access violation; not modelled)");
    return rt_slist_pop(ListHead);
}
