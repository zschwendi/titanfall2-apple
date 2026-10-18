/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iagetinputlayout
 * RT-CASES:  a slot never set reads NULL; a Get returns the bound layout and adds one reference; each Get hands out one more reference; a NULL unbinds, and a Get then reads NULL and adds nothing; a Get after another Set returns the new layout
 * RT-TRAPS:  IAGetInputLayout with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented
 * by one", the reference a Get hands out.  The page marks ppInputLayout [out], not optional, so a
 * NULL out-pointer is a stop, as the notes say. */
COM_METHOD(ID3D11DeviceContext, void, IAGetInputLayout, ID3D11InputLayout **ppInputLayout)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!ppInputLayout)
        RT_UNIMPLEMENTED(ctx, "IAGetInputLayout with a NULL out-pointer");
    *ppInputLayout = rt_nulldev_ref(c->input_layout);
}
