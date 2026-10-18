/* RT-STATUS: partial
 * RT-DOC:    https://github.com/NVIDIAGameWorks/HBAOPlus/blob/master/include/GFSDK_SSAO.h
 * RT-CASES:  answers GFSDK_SSAO_D3D_FEATURE_LEVEL_NOT_SUPPORTED (14) and leaves the context pointer as it was; logs one warning line
 * RT-TRAPS:  GFSDK_SSAO_CreateContext_D3D11 with a NULL device or a NULL context pointer
 */
#include "rt/shim.h"
#include "rt/log.h"

/* NVIDIA's ambient-occlusion library is not lifted (PLAN.md) and nothing stands in for it:
 * the device draws nothing.  THE PLANNER'S DECISION (brief of unit d3d-resources, item 7):
 * this one function reports that it cannot make a context, so the game goes on without the
 * effect; every other function of the library stays a trap stub.
 *
 * Why the game tolerates it (read at the call site and at the global's four other
 * references, all in materialsystem_dx11): the status is never read: the caller at +0x296dd
 * returns at once and its own caller (+0x16e44) calls the next function.  What the game looks
 * at is the context pointer it passed (the global at rva 0x19acb98, zero in the image): the
 * function that would use the effect (+0x288b0) tests it for NULL at its first instructions
 * and leaves when it is; the shutdown function (+0x2a000) releases it only when it is not.
 * So the pointer is left exactly as it was.
 *
 * The status: RT-DOC, NVIDIA's public header, version 4.0 (read 2026-10-10 by a subagent):
 * GFSDK_SSAO_D3D_FEATURE_LEVEL_NOT_SUPPORTED is the status the function's comment names for a
 * device it does not support (below feature level 11_0), the fifteenth enumerator of
 * GFSDK_SSAO_Status, no enumerator having an explicit value: 14.  The game's library is an
 * older version than that header (its call passes another version number), in which the
 * number may differ; nothing reads it.  The fourth argument is the header's version
 * structure, 16 bytes, passed as a pointer to the caller's copy.
 *
 * The header gives GFSDK_SSAO_NULL_ARGUMENT for a NULL device or context pointer; the game
 * passes neither, so that is a stop rather than a second answer nobody asked for. */
SHIM(gfsdk_ssao_win64, UINT, GFSDK_SSAO_CreateContext_D3D11, void *pD3DDevice, void **ppContext, const void *pCustomHeap, const void *pHeaderVersion)
{
    (void)pCustomHeap;
    (void)pHeaderVersion;
    if (!pD3DDevice || !ppContext)
        RT_UNIMPLEMENTED(ctx, "GFSDK_SSAO_CreateContext_D3D11 with a NULL device or a NULL context pointer (%p, %p)", pD3DDevice, (void *)ppContext);
    rt_log(RT_CH_IMPORT, RT_LL_WARN, "GFSDK_SSAO_CreateContext_D3D11: no ambient-occlusion library here; answering "
           "GFSDK_SSAO_D3D_FEATURE_LEVEL_NOT_SUPPORTED (14) and leaving the context pointer at %p as it is", (void *)ppContext);
    return 14;
}
