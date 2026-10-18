/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3dcommon/nf-d3dcommon-id3d10blob-getbuffersize
 * RT-CASES:  the number of bytes of the blob's data
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-11: "Gets the size."; the return value is "The size of the data, in
 * bytes."  The game calls this once per input layout, at materialsystem_dx11+0x3144a. */
COM_METHOD(ID3D10Blob, SIZE_T, GetBufferSize)
{
    NdBlob *b = rt_com_state(ctx, This, &rt_nulldev_blob_class);
    return b->size;
}
