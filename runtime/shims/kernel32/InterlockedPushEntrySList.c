/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/interlockedapi/nf-interlockedapi-interlockedpushentryslist
 * RT-CASES:  returns NULL for an empty list; returns the previous first entry; the entry becomes the first, its Next is the previous first; the depth in the low 16 bits counts up; does not change last error
 * RT-TRAPS:  NULL list header; NULL list entry
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, PSLIST_ENTRY, InterlockedPushEntrySList, PSLIST_HEADER ListHead, PSLIST_ENTRY ListEntry)
{
    if (!ListHead)  RT_UNIMPLEMENTED(ctx, "NULL list header (Windows raises an access violation; not modelled)");
    if (!ListEntry) RT_UNIMPLEMENTED(ctx, "NULL list entry (Windows raises an access violation; not modelled)");
    return rt_slist_push(ListHead, ListEntry);
}
