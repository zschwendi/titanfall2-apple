/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getmixformat
 * RT-CASES:  NULL pointer E_POINTER; else a 40-byte WAVEFORMATEXTENSIBLE of the made-up endpoint, S_OK
 * RT-TRAPS:  none
 */
#include "rt/heap.h"
#include "rt/mmdev.h"
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "The caller is responsible for freeing the storage, when it is no longer
 * needed, by calling the CoTaskMemFree function."  The block comes from rt_galloc, the process heap
 * that CoTaskMemFree frees (as IMMDevice::GetId's does).  A NULL ppDeviceFormat is E_POINTER (the
 * brief's rule for every out-pointer of this interface). */
COM_METHOD(IAudioClient, HRESULT, GetMixFormat, WAVEFORMATEX **ppDeviceFormat)
{
    (void)rt_com_state(ctx, This, &rt_audio_client_class);
    if (!ppDeviceFormat) return E_POINTER;
    void *fmt = rt_galloc(AC_MIX_FORMAT_BYTES);
    ac_fill_mix_format(fmt);
    *ppDeviceFormat = fmt;
    return S_OK;
}
