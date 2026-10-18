/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetinputlayout
 * RT-CASES:  a slot never set reads NULL; a Set binds the layout and holds one reference to it; a Set over a bound layout releases the old one; a NULL unbinds the slot and releases the layout; a NULL over an empty slot does nothing
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference the bound layout holds.  The page marks pInputLayout [in, optional], so a NULL
 * unbinds, as the notes say. */
COM_METHOD(ID3D11DeviceContext, void, IASetInputLayout, ID3D11InputLayout *pInputLayout)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    rt_nulldev_bind(&c->input_layout,
                    rt_nulldev_arg(ctx, pInputLayout, &rt_nulldev_inputlayout_class, "pInputLayout"));
    RT_BACK(ctx, IASetInputLayout, c->child.back, rt_d3dback_of(pInputLayout));
}
