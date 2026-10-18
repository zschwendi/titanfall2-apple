/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createpixelshader
 * RT-CASES:  bytecode of 64 bytes beginning with DXBC: S_OK, one reference, the device held, the length kept; a NULL out-pointer validates only: S_FALSE and no object
 * RT-TRAPS:  with a class linkage; is NULL, shorter than 4 bytes or does not begin with DXBC
 */
#include "rt/nulldev.h"

/* Nothing is compiled or translated: the pixel shader is a record of its byte count.  The
 * pages of CreateVertexShader and CreatePixelShader, read 2026-10-10 (by a subagent that
 * returned the text): pClassLinkage "the value can be NULL"; the out-pointer: "If this is
 * NULL, all other parameters will be validated, and if all parameters pass validation this API
 * will return S_FALSE instead of S_OK."  The other two shader creators' pages were not read:
 * the same is assumed of them (from memory).  The stops: rt_nulldev_shader_create
 * (rt/nulldev.h): a class linkage (the game never passes one: finding 31), bytecode that is
 * not a compiled shader. */
COM_METHOD(ID3D11Device, HRESULT, CreatePixelShader, const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11PixelShader **ppPixelShader)
{
    HRESULT hr = rt_nulldev_shader_create(ctx, This, &rt_nulldev_ps_class, pShaderBytecode, BytecodeLength, pClassLinkage, (void **)ppPixelShader);
    RT_BACK_CREATE(ctx, ppPixelShader ? *ppPixelShader : NULL, CreatePixelShader, rt_d3dback_of(This), pShaderBytecode, BytecodeLength);
    return hr;
}
