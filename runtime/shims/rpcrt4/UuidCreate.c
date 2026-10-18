/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/rpcdce/nf-rpcdce-uuidcreate
 * RT-CASES:  writes a version-4 random UUID and returns RPC_S_OK; writes exactly the sixteen bytes of the UUID; two calls give different UUIDs; does not change last error
 * RT-TRAPS:  NULL Uuid
 */
#include "rt/shim.h"
#include "rt/machine.h"
#include "rt/win/com.h"

SHIM(rpcrt4, RPC_STATUS, UuidCreate, UUID *Uuid)
{
    if (Uuid == NULL) RT_UNIMPLEMENTED(ctx, "UuidCreate with NULL Uuid");
    /* Sixteen random bytes, then the version (4) and the variant (10xx) of a random UUID.
     * Windows makes version-4 UUIDs here: the page says the result cannot be traced to the
     * ethernet address of the computer. */
    rt_random_bytes(Uuid, sizeof *Uuid);
    Uuid->Data3 = (WORD)((Uuid->Data3 & 0x0FFF) | 0x4000);
    Uuid->Data4[0] = (BYTE)((Uuid->Data4[0] & 0x3F) | 0x80);
    return RPC_S_OK;
}
