/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgidevice1-setmaximumframelatency
 * RT-CASES:  1 to 16 is stored and S_OK; 0 stores the default 3 and S_OK
 * RT-TRAPS:  SetMaximumFrameLatency(
 */
#include "rt/nulldev.h"

/* RT-DOC, not read (no page fetch was available): the notes of this unit say, from memory,
 * "The value defaults to 3, but can range from 1 to 16. A value of 0 will reset latency to the
 * default."  So 1..16 is stored, 0 stores 3 (NdDevice.max_frame_latency starts at 3, the
 * documented default), and the result is S_OK.  Above 16 the page's failure code is unknown
 * to this unit: a stop.  Nothing presents frames, so the stored number has no other effect. */
COM_METHOD(IDXGIDevice1, HRESULT, SetMaximumFrameLatency, UINT MaxLatency)
{
    NdDevice *s = rt_com_state(ctx, This, &rt_nulldev_device_class);
    if (MaxLatency > 16) RT_UNIMPLEMENTED(ctx, "SetMaximumFrameLatency(%u)", MaxLatency);
    s->max_frame_latency = MaxLatency == 0 ? 3 : MaxLatency;
    RT_BACK_SAME(ctx, S_OK, SetMaximumFrameLatency, s->back, MaxLatency);
    return S_OK;
}
