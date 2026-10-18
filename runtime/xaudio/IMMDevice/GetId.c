/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevice-getid
 * RT-CASES:  NULL E_POINTER; else the made-up endpoint's id as a null-terminated wide string in memory CoTaskMemFree frees, S_OK
 * RT-TRAPS:  none
 */
#include <string.h>

#include "rt/heap.h"
#include "rt/mmdev.h"

/* RT-DOC: the method "allocates the storage for the string", and the caller frees it with
 * CoTaskMemFree; E_POINTER when ppstrId is NULL.  The block comes from rt_galloc, the process
 * heap that CoTaskMemFree frees (as IAudioClient::GetMixFormat's does).  The id is the one
 * made-up endpoint's (MM_ENDPOINT_ID), which IMMDeviceEnumerator::GetDevice takes back.
 * The game's audio library asks for it right after it registered for endpoint notifications
 * (mileswin64+0x3be6b, the planner's run of 12 October 2026). */
COM_METHOD(IMMDevice, HRESULT, GetId, LPWSTR *ppstrId)
{
    (void)rt_com_state(ctx, This, &rt_mm_device_class);
    if (!ppstrId) return E_POINTER;
    static const char id[] = MM_ENDPOINT_ID;
    WCHAR *w = rt_galloc(sizeof id * sizeof(WCHAR));        /* zeroed: the terminator is there */
    for (size_t i = 0; i + 1 < sizeof id; i++) w[i] = (WCHAR)(unsigned char)id[i];
    *ppstrId = w;
    return S_OK;
}
