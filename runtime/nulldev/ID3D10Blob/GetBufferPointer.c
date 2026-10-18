/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3dcommon/nf-d3dcommon-id3d10blob-getbufferpointer
 * RT-CASES:  the blob's bytes, the same pointer on every call
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-11: "Gets a pointer to the data."  The data is the blob's own block
 * (NdBlob), valid until the blob is destroyed.  The game calls this once per input layout,
 * at materialsystem_dx11+0x31456, and passes the pointer to CreateInputLayout. */
COM_METHOD(ID3D10Blob, void *, GetBufferPointer)
{
    NdBlob *b = rt_com_state(ctx, This, &rt_nulldev_blob_class);
    return b->data;
}
