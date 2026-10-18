/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/interlockedapi/nf-interlockedapi-querydepthslist
 * RT-CASES:  returns 0 for an empty list; returns the depth in the low 16 bits, up to 65535; ignores the sequence above the depth; the header is not changed; does not change last error
 * RT-TRAPS:  NULL list header
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, USHORT, QueryDepthSList, PSLIST_HEADER ListHead)
{
    if (!ListHead) RT_UNIMPLEMENTED(ctx, "NULL list header (Windows raises an access violation; not modelled)");
    return rt_slist_depth(ListHead);
}
