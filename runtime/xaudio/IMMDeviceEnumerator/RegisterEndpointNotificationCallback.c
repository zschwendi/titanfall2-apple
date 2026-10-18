/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdeviceenumerator-registerendpointnotificationcallback
 * RT-CASES:  NULL E_POINTER; else the pointer's value is remembered and S_OK; a ninth for one enumerator is a stop
 * RT-TRAPS:  core: a ninth registration (the list holds 8)
 */
#include "rt/log.h"
#include "rt/mmdev.h"

/* RT-DOC, read 2026-10-12: E_POINTER "Parameter pNotify is NULL."  The client is the game's
 * object: the made-up endpoint never changes, so the client is never called and never read;
 * its value is only kept, so Unregister can find it.  The log line is written once. */
COM_METHOD(IMMDeviceEnumerator, HRESULT, RegisterEndpointNotificationCallback, IMMNotificationClient *pClient)
{
    static int logged;
    MmEnumerator *e = rt_com_state(ctx, This, &rt_mm_enumerator_class);
    if (!pClient) return E_POINTER;
    if (!rt_mm_callbacks_add(e, pClient)) rt_com_stop(ctx);
    if (!logged) {
        logged = 1;
        rt_log(RT_CH_COM, RT_LL_INFO, "MMDevice: a notification client is registered; the made-up endpoint never changes, so it is never called");
    }
    return S_OK;
}
