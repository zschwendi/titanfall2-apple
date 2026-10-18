/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersinfo
 * RT-CASES:  two calls with a 704-byte buffer and sizes 0 then 704 return ERROR_NO_DATA and change nothing; a NULL buffer with a size returns ERROR_NO_DATA; a NULL size pointer returns ERROR_INVALID_PARAMETER; last error is left alone; logs that the machine has no network adapter
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(iphlpapi, ULONG, GetAdaptersInfo, PVOID AdapterInfo, PULONG SizePointer)
{
    /* This machine has no network adapter, so no IP_ADAPTER_INFO is ever written: the answer
     * is the documented one for a computer with no adapter information (the net contract's
     * decision, "The adapter query"). The loopback interface is not reported by this call. */
    rt_log_once(RT_CH_NET, RT_LL_DEBUG, "iphlpapi.dll!GetAdaptersInfo",
                "GetAdaptersInfo: the machine has no network adapter");
    (void)AdapterInfo;
    if (!SizePointer) return ERROR_INVALID_PARAMETER;
    return ERROR_NO_DATA;
}
