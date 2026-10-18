/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createinputlayout
 * RT-CASES:  three elements: S_OK, one reference, the device held, the elements kept as a copy; a NULL out-pointer validates only: S_FALSE and no object
 * RT-TRAPS:  CreateInputLayout with 33 elements; CreateInputLayout with bytecode
 */
#include <string.h>

#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text): ppInputLayout: "To validate
 * the other input parameters, set this pointer to be NULL and verify that the method returns
 * S_FALSE."; "If the method succeeds, the return code is S_OK."  The page says the shader's
 * input signature "is validated against the array of elements"; nothing is parsed here, so
 * no layout is ever refused for not matching: the elements are kept (a copy; each
 * SemanticName stays the game's pointer) and the bytecode is only checked to begin as a
 * compiled shader does ("DXBC"; from memory).  More than 32 elements (the input assembler's
 * limit, D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT; from memory) is a stop. */
COM_METHOD(ID3D11Device, HRESULT, CreateInputLayout, const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout **ppInputLayout)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_device_class);
    if (NumElements > 32 || (NumElements && !pInputElementDescs))
        RT_UNIMPLEMENTED(ctx, "CreateInputLayout with %u elements at %p", (unsigned)NumElements, (const void *)pInputElementDescs);
    if (!pShaderBytecodeWithInputSignature || BytecodeLength < 4 || memcmp(pShaderBytecodeWithInputSignature, "DXBC", 4))
        RT_UNIMPLEMENTED(ctx, "CreateInputLayout with bytecode (%p, %llu bytes) that is NULL, shorter than 4 bytes or does not begin "
                         "with DXBC", pShaderBytecodeWithInputSignature, (unsigned long long)BytecodeLength);
    if (!ppInputLayout) {
        RT_BACK_ALONE(ctx, CreateInputLayout);  /* the call only validated: no object, so the back end is not asked */
        return S_FALSE;
    }
    ID3D11InputLayout *layout = rt_nulldev_new(This, &rt_nulldev_inputlayout_class);
    rt_nulldev_inputlayout_set(layout, pInputElementDescs, NumElements);
    rt_nulldev_inputlayout_log(layout, pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength);
    /* The seam (rt/d3dback.h, rules 3 and 4): the front has its object and its answer; the back
     * end makes its own from the game's elements and bytecode, unchanged. */
    RT_BACK_CREATE(ctx, layout, CreateInputLayout, rt_d3dback_of(This), pInputElementDescs, NumElements,
                   pShaderBytecodeWithInputSignature, BytecodeLength);
    *ppInputLayout = layout;
    return S_OK;
}
