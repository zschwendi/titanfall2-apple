/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevice-activate
 * RT-CASES:  NULL ppv E_POINTER; IAudioClient: a new audio client, S_OK; any other interface: NULL, E_NOINTERFACE, one log line
 * RT-TRAPS:  none
 */
#include <stdio.h>

#include "rt/log.h"
#include "rt/mmdev.h"
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: E_POINTER "ppInterface is NULL."; "An Activate call to activate an
 * IDirectSound or IDirectSoundCapture8 interface on a capture device ... fails and returns error
 * code E_NOINTERFACE."  The made-up endpoint answers IAudioClient and nothing else.  dwClsCtx and
 * pActivationParams are not used: the audio client has no loopback mode and no activation
 * parameters yet. */
COM_METHOD(IMMDevice, HRESULT, Activate, REFIID iid, DWORD dwClsCtx, PROPVARIANT *pActivationParams, void **ppv)
{
    (void)rt_com_state(ctx, This, &rt_mm_device_class);
    (void)dwClsCtx;
    (void)pActivationParams;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (iid && rt_com_iface_by_iid(iid) == &rt_iface_IAudioClient) {
        *ppv = rt_audio_client_new();
        return S_OK;
    }
    char text[40];
    if (!iid) snprintf(text, sizeof text, "(NULL)");
    else snprintf(text, sizeof text, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", (unsigned)iid->Data1,
                  (unsigned)iid->Data2, (unsigned)iid->Data3, iid->Data4[0], iid->Data4[1], iid->Data4[2],
                  iid->Data4[3], iid->Data4[4], iid->Data4[5], iid->Data4[6], iid->Data4[7]);
    rt_log(RT_CH_COM, RT_LL_INFO, "MMDevice::Activate: interface %s is not one the endpoint has (only IAudioClient); E_NOINTERFACE", text);
    return E_NOINTERFACE;
}
