/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11shader/nf-d3d11shader-id3d11shaderreflection-getinputparameterdesc
 * RT-CASES:  each element of the signature, field by field, S_OK; the 40 bytes of d3dcompiler_46's structure and nothing after them; an index past the end is E_INVALIDARG and writes nothing; a NULL description is E_INVALIDARG
 * RT-TRAPS:  none
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rt/nulldev.h"

/* Element `index` of the shader's input signature, as D3DReflect parsed it (NdReflection).
 *
 * THE FIELDS, each from the signature element of the container (docs/research/dxbc-shaders.md,
 * 1.4 and 3.5): the semantic name (a string the object owns: runtime/nulldev/reflection.c says
 * how long the pointer is valid) and index, the register, the system value and the component
 * type as the chunk stores them (the adjustment research 1.4 describes is for a pixel
 * shader's outputs only), the mask, the second mask byte unchanged (of an input: the
 * components the shader always reads; RT-DOC's structure page calls it ReadWriteMask), and
 * the stream and the minimum precision, which an ISGN element does not have and are 0 (an
 * ISG1 element has both).
 *
 * THE STRUCTURE is d3dcompiler_46's: 40 bytes, MinPrecision at offset 32 (rt/win/d3d11.h;
 * MinGW's d3d11shader.h has the field from D3D_COMPILER_VERSION 46 on, and Wine's test of a
 * vertex shader's signature expects Windows to write it as 0 from version 46 on and to leave
 * it untouched before: research 3.5, 3.8).  The game's frame has the 40 bytes
 * (materialsystem_dx11+0x323b0 passes rsp+0x20, and its next local is at rsp+0x50).  The six
 * padding bytes (26, 27, 36 to 39) are not written: no source says what Windows leaves there.
 *
 * AN INDEX PAST THE END, AND A NULL DESCRIPTION: E_INVALIDARG.  RT-DOC (read 2026-10-11) names
 * no code.  Wine's tests of the interface (dlls/d3dcompiler_43/tests/reflection.c, read
 * 2026-10-11 from Wine's mirror on GitHub) expect E_INVALIDARG from Windows for index 0xffffffff
 * and for a NULL description, and the game's loop over the signature ends on exactly that
 * value and on nothing else (materialsystem_dx11+0x323f6 and +0x32635 compare the result with
 * 0x80070057). */
COM_METHOD(ID3D11ShaderReflection, HRESULT, GetInputParameterDesc, UINT index, D3D11_SIGNATURE_PARAMETER_DESC *desc)
{
    NdReflection *r = rt_com_state(ctx, This, &rt_nulldev_reflection_class);
    if (!desc || index >= r->ninputs) return E_INVALIDARG;
    const D3D11_SIGNATURE_PARAMETER_DESC *e = &r->inputs[index];
    /* The nine fields, each at its offset, and no other byte of the caller's structure. */
#define PUT(field) memcpy((uint8_t *)desc + offsetof(D3D11_SIGNATURE_PARAMETER_DESC, field), &e->field, sizeof e->field)
    PUT(SemanticName);
    PUT(SemanticIndex);
    PUT(Register);
    PUT(SystemValueType);
    PUT(ComponentType);
    PUT(Mask);
    PUT(ReadWriteMask);
    PUT(Stream);
    PUT(MinPrecision);
#undef PUT
    return S_OK;
}
