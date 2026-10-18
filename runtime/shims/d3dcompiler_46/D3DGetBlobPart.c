/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dgetblobpart
 * RT-CASES:  the input-signature part of a container: S_OK and a blob with one reference that holds a container of that one chunk; the caller's bytes are not kept
 * RT-TRAPS:  D3DGetBlobPart with no source data; D3DGetBlobPart with a NULL out-pointer; D3DGetBlobPart for a part other than the input signature; D3DGetBlobPart with flags; D3DGetBlobPart of a container that does not parse
 */
#include "rt/shim.h"
#include "rt/nulldev.h"

/* The game's one call (materialsystem_dx11+0x2f519, after it has reflected a vertex shader):
 * the shader's bytes and length, part 0, flags 0, and a slot of its table of signature blobs.
 * It later asks the blob for its size and pointer and gives them to CreateInputLayout
 * (+0x3144a, +0x31456).  runtime/nulldev/blob.c makes the part and says what confirms its form.
 *
 * `Part` is a D3D_BLOB_PART; 0 is D3D_BLOB_INPUT_SIGNATURE_BLOB, the enumeration's first value
 * (MinGW's d3dcompiler.h).  The type is written UINT here because the runtime's type list
 * has no name for it (its header is not one the descriptions are generated from).
 *
 * RT-DOC (read 2026-10-11): "Retrieves a specific part from a compilation result."; Flags:
 * "Currently, no flags are defined."; the result is "one of the Direct3D 11 return codes",
 * none named.  The game does not look at the result (it goes on to leave its lock and
 * return the table index), so every case that is not the game's is a stop here and not an
 * error code: another part, flags, a NULL pointer, and bytes that are no container or have
 * no single ISGN chunk; the stop's text names the check that failed. */
SHIM(d3dcompiler_46, HRESULT, D3DGetBlobPart, LPCVOID pSrcData, SIZE_T SrcDataSize, UINT Part, UINT Flags, ID3D10Blob **ppPart)
{
    char why[RT_DXBC_WHY];

    if (!pSrcData || !SrcDataSize)
        RT_UNIMPLEMENTED(ctx, "D3DGetBlobPart with no source data (%p, %llu bytes)", pSrcData, (unsigned long long)SrcDataSize);
    if (!ppPart) RT_UNIMPLEMENTED(ctx, "D3DGetBlobPart with a NULL out-pointer");
    if (Part != 0) RT_UNIMPLEMENTED(ctx, "D3DGetBlobPart for a part other than the input signature (D3D_BLOB_PART %u)", Part);
    if (Flags) RT_UNIMPLEMENTED(ctx, "D3DGetBlobPart with flags %#x", Flags);
    ID3D10Blob *blob = rt_nulldev_signature_blob_new(pSrcData, SrcDataSize, why);
    if (!blob)
        RT_UNIMPLEMENTED(ctx, "D3DGetBlobPart of a container that does not parse (%p, %llu bytes): %s", pSrcData,
                         (unsigned long long)SrcDataSize, why);
    *ppPart = blob;
    return S_OK;
}
