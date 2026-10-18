/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dreflect
 * RT-CASES:  a container with an input signature, asked for ID3D11ShaderReflection: S_OK and a reflection object with one reference; the caller's bytes are not kept; an ISG1 signature is read as well
 * RT-TRAPS:  D3DReflect with no source data; D3DReflect with a NULL interface ID or out-pointer; D3DReflect for an interface other than; D3DReflect of a container that does not parse
 */
#include <string.h>

#include "rt/shim.h"
#include "rt/nulldev.h"

/* The game's one call (materialsystem_dx11+0x323d8): a vertex shader's bytes and their length,
 * the interface ID at materialsystem_dx11+0x1da8f0, an out-pointer on its stack.  Nothing is
 * compiled: the answer is in the bytes (rt/dxbc.h reads the container, runtime/nulldev/
 * reflection.c keeps its input signature).
 *
 * THE INTERFACE ID.  The one of d3dcompiler_46 and _47, {8d536ca1-0cca-4956-a837-786963755584}
 * (MinGW's d3d11shader.h gives three by compiler version; research 3.1), which is the ID of
 * the description runtime/com/desc/ID3D11ShaderReflection.idef.  Any other is a stop.
 *
 * WHAT IS A STOP AND WHY NOT AN ERROR CODE.  RT-DOC (read 2026-10-11) says only "Returns one
 * of the Direct3D 11 return codes" and names none for NULL data, a length of 0, another
 * interface ID or bytes that are no container.  Wine's tests record what Windows answers for
 * some of those per library version (research 3.1), but the game's caller does not look at
 * the result: it loads the out-pointer and calls through it at once (+0x323dd).  An error
 * returned here would become a fault in the game's code one instruction later, so each of
 * these stops here, with the caller and the reason.  A container that does not parse is
 * never answered by guessing: the stop's text is the check that failed (rt/dxbc.h lists
 * them; the checksum is one). */
SHIM(d3dcompiler_46, HRESULT, D3DReflect, LPCVOID pSrcData, SIZE_T SrcDataSize, REFIID pInterface, void **ppReflector)
{
    char why[RT_DXBC_WHY];

    if (!pSrcData || !SrcDataSize)
        RT_UNIMPLEMENTED(ctx, "D3DReflect with no source data (%p, %llu bytes)", pSrcData, (unsigned long long)SrcDataSize);
    if (!pInterface || !ppReflector)
        RT_UNIMPLEMENTED(ctx, "D3DReflect with a NULL interface ID or out-pointer (%p, %p)", (const void *)pInterface, (void *)ppReflector);
    if (memcmp(pInterface, &rt_iface_ID3D11ShaderReflection.iid, sizeof(GUID))) {
        const uint8_t *g = (const uint8_t *)pInterface;
        RT_UNIMPLEMENTED(ctx, "D3DReflect for an interface other than d3dcompiler_46's ID3D11ShaderReflection: "
                         "{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                         g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    }
    ID3D11ShaderReflection *reflection = rt_nulldev_reflection_new(pSrcData, SrcDataSize, why);
    if (!reflection)
        RT_UNIMPLEMENTED(ctx, "D3DReflect of a container that does not parse (%p, %llu bytes): %s", pSrcData,
                         (unsigned long long)SrcDataSize, why);
    *ppReflector = reflection;
    return S_OK;
}
