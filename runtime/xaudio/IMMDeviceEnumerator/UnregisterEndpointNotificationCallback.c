/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdeviceenumerator-unregisterendpointnotificationcallback
 * RT-CASES:  NULL E_POINTER; a registered pointer: forgotten, S_OK; any other: E_NOTFOUND
 * RT-TRAPS:  none
 */
#include "rt/mmdev.h"

/* RT-DOC, read 2026-10-12: E_POINTER "Parameter pNotify is NULL."; E_NOTFOUND "The specified
 * notification interface was not found." */
COM_METHOD(IMMDeviceEnumerator, HRESULT, UnregisterEndpointNotificationCallback, IMMNotificationClient *pClient)
{
    MmEnumerator *e = rt_com_state(ctx, This, &rt_mm_enumerator_class);
    if (!pClient) return E_POINTER;
    return rt_mm_callbacks_remove(e, pClient) ? S_OK : E_NOTFOUND;
}
