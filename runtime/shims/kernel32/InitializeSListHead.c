/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/interlockedapi/nf-interlockedapi-initializeslisthead
 * RT-CASES:  empties a header that held entries; a zeroed header stays empty; does not change last error
 * RT-TRAPS:  NULL list header
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, InitializeSListHead, PSLIST_HEADER ListHead)
{
    if (!ListHead) RT_UNIMPLEMENTED(ctx, "NULL list header (Windows raises an access violation; not modelled)");
    rt_slist_init(ListHead);
}
