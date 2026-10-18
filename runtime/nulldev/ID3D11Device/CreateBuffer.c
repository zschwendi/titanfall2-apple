/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createbuffer
 * RT-CASES:  a buffer of 256 bytes: S_OK, one reference, the device held, the description kept as passed, one subresource; initial data is copied into the buffer; a NULL out-pointer validates only: S_FALSE and no object left
 * RT-TRAPS:  CreateBuffer with a NULL description; a buffer description with a size of 0; CPU write access without dynamic or staging usage; CPU read access without staging usage; immutable usage without initial data; a usage that is not one of D3D11_USAGE; a miscellaneous flag the device does not know; initial data whose pSysMem is NULL
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text): pInitialData: "use NULL to
 * allocate space only (with the exception that it cannot be NULL if the usage flag is
 * D3D11_USAGE_IMMUTABLE)"; ppBuffer: "Set this parameter to NULL to validate the other input
 * parameters (S_FALSE indicates a pass)."  D3D11_BUFFER_DESC, ByteWidth: "Size of the buffer in
 * bytes."  S_OK for success is from memory (the page names only E_OUTOFMEMORY).
 *
 * The record keeps the six fields as passed and one subresource of ByteWidth bytes; initial
 * data is ByteWidth bytes at pSysMem (the pitches have "no meaning for the other resource
 * types": D3D11_SUBRESOURCE_DATA).  Every stop of a description is rt_nulldev_buffer_new's
 * (rt/nulldev.h).  The page names no result for a NULL description: a stop.  The rule that a
 * constant buffer's size is a multiple of 16 is not checked: nothing here depends on it. */
COM_METHOD(ID3D11Device, HRESULT, CreateBuffer, const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Buffer **ppBuffer)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_device_class);
    if (!pDesc) RT_UNIMPLEMENTED(ctx, "CreateBuffer with a NULL description");
    ID3D11Buffer *buffer = rt_nulldev_buffer_new(ctx, This, pDesc, pInitialData != NULL);
    if (pInitialData) rt_nulldev_fill(ctx, buffer, pInitialData);
    if (!ppBuffer) {
        rt_com_release(buffer);
        RT_BACK_ALONE(ctx, CreateBuffer);       /* the call only validated: no object, so the back end is not asked */
        return S_FALSE;
    }
    /* The seam (rt/d3dback.h, rules 3 and 4): the front has its object and its answer; the back
     * end makes its own from the game's description and data, unchanged. */
    RT_BACK_CREATE(ctx, buffer, CreateBuffer, rt_d3dback_of(This), pDesc, pInitialData);
    *ppBuffer = buffer;
    return S_OK;
}
