/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/rpcdce/nf-rpcdce-rpcstringfreea
 * RT-CASES:  frees a string and sets the pointer to NULL; a second free of the NULL pointer returns RPC_S_OK; does not change last error
 * RT-TRAPS:  NULL String; core: a pointer that is not a live block (misaligned); core: a pointer that is not a live block (outside guest memory); core: a pointer that is not a live block (already freed); core: a pointer that is not a live block (a block of another heap); core: a pointer that is not a live block (an interior pointer); core: a pointer that is not a live block (a block of a destroyed heap)
 */
#include "rt/shim.h"
#include "rt/heap.h"
#include "rt/win/com.h"

SHIM(rpcrt4, RPC_STATUS, RpcStringFreeA, RPC_CSTR *String)
{
    if (!String)
        RT_UNIMPLEMENTED(ctx, "RpcStringFreeA with a NULL String");
    if (*String) {
        rt_heap_free(rt_heap_process(), 0, *String);
        *String = NULL;
    }
    return RPC_S_OK;
}
