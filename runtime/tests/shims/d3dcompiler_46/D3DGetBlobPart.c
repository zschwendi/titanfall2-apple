/* Expected values, none from the shim.
 *   The part: the 120 bytes below were written by the repository's other container writer,
 *   tools/dxmt/make_dxbc.py (another unit's, with its own checksum code):
 *       container([(b"ISGN", signature([("POSITION", 0, 0, 3, 0, 7, 7), ("TEXCOORD", 0, 0, 3, 1, 3, 3)]))])
 *   A container that holds one ISGN chunk at offset 36 is the form of the input-signature
 *   part (Wine's test_get_blob_part, run on Windows: docs/research/dxbc-shaders.md, 3.9), and
 *   120 bytes is the length the recorded game passes to CreateInputLayout for a layout of
 *   POSITION and TEXCOORD (docs/trace/d3d-creators.md, call 8745).  The test puts that chunk's
 *   data into a larger container (rt/test_dxbc.h) between two others and asks for the part.
 *   S_OK and part 0 with flags 0: the game's call (materialsystem_dx11+0x2f519).  One
 *   reference: COM's rule for an object handed out (from memory).
 *   The stops: the shim's own decisions (its comment says why each is a stop and not a code). */
#include <string.h>

#include "rt/test.h"
#include "rt/test_dxbc.h"
#include "rt/nulldev.h"

static const uint8_t expected[120] = {
    0x44, 0x58, 0x42, 0x43, 0x3d, 0x8a, 0x5f, 0xf3, 0xf7, 0x74, 0x83, 0x2a, 0xcd, 0x8a, 0x77, 0x2d,
    0x70, 0x0e, 0x36, 0x95, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x24, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x4e, 0x4c, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x07, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x03, 0x03, 0x00, 0x00, 0x50, 0x4f, 0x53, 0x49, 0x54, 0x49, 0x4f, 0x4e, 0x00, 0x54, 0x45, 0x58,
    0x43, 0x4f, 0x4f, 0x52, 0x44, 0x00, 0x00, 0x00,
};

TEST(d3dcompiler_46, D3DGetBlobPart)
{
    uint8_t c[T_DXBC_MAX], m[T_DXBC_MAX], filler[40], wide[T_DXBC_MAX];
    static const TDxbcElement one[] = { { "POSITION", 0, 0, 3, 0, 0x7, 0x7, 0, 0 } };
    ID3D10Blob *blob = (ID3D10Blob *)(uintptr_t)0x0ed0c101;         /* the game's slot holds leftovers */
    RtDxbc d;
    char why[RT_DXBC_WHY];

    memset(filler, 0x3c, sizeof filler);
    const uint8_t *isgn = expected + 44;                            /* the chunk's data: 76 bytes */
    const TDxbcChunk chunks[] = { { "RDEF", filler, 40 }, { "ISGN", isgn, 76 }, { "OSGN", filler, 12 }, { "SHDR", filler, 33 } };
    uint32_t size = t_dxbc_container(c, chunks, 4);

    T_CASE("the input-signature part of a container: S_OK and a blob with one reference that holds a container of that one chunk");
    T_EQ(CALL(d3dcompiler_46, D3DGetBlobPart, c, size, 0, 0, &blob), S_OK);
    T_REQUIRE(blob != NULL && rt_com_class_of(blob) == &rt_nulldev_blob_class);
    T_EQ(LD64((uint64_t)(uintptr_t)blob), rt_iface_ID3D10Blob.vtable);
    T_EQ(rt_com_refs(blob), 1);
    T_EQ(COM_CALL(ID3D10Blob, GetBufferSize, blob), 120);
    const uint8_t *part = COM_CALL(ID3D10Blob, GetBufferPointer, blob);
    T_REQUIRE(part != NULL);
    T_TRUE(!memcmp(part, expected, 120));                           /* every byte, the checksum among them */
    /* The part is itself a container of one chunk, and asking it for its part gives the same bytes. */
    T_REQUIRE(rt_dxbc_open(part, 120, &d, why));
    T_EQ(d.nchunks, 1);
    ID3D10Blob *again = NULL;
    T_EQ(CALL(d3dcompiler_46, D3DGetBlobPart, part, 120, 0, 0, &again), S_OK);
    T_REQUIRE(again != NULL && again != blob);
    T_EQ(COM_CALL(ID3D10Blob, GetBufferSize, again), 120);
    T_TRUE(!memcmp(COM_CALL(ID3D10Blob, GetBufferPointer, again), expected, 120));
    T_EQ(COM_CALL(ID3D10Blob, Release, again), 0);
    /* What the game does with the part next: CreateInputLayout takes it with the layout's elements
     * (the recorded call 8745: POSITION as R32G32B32_FLOAT, format 6, and TEXCOORD as R32G32_FLOAT, 16). */
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    const D3D11_INPUT_ELEMENT_DESC el[2] = { { "POSITION", 0, 6, 0, 0, 0, 0 }, { "TEXCOORD", 0, 16, 0, 28, 0, 0 } };
    ID3D11InputLayout *layout = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateInputLayout, device, el, 2, part, 120, &layout), S_OK);
    T_TRUE(layout != NULL);
    /* One element named POSITION makes 88 bytes: the recording's length for that layout. */
    uint8_t s1[64];
    const TDxbcChunk small[] = { { "ISGN", s1, t_dxbc_signature(s1, false, one, 1) }, { "SHDR", filler, 8 } };
    T_EQ(CALL(d3dcompiler_46, D3DGetBlobPart, m, t_dxbc_container(m, small, 2), 0, 0, &again), S_OK);
    T_EQ(COM_CALL(ID3D10Blob, GetBufferSize, again), 88);
    T_EQ(COM_CALL(ID3D10Blob, Release, again), 0);

    T_CASE("the caller's bytes are not kept");
    memset(c, 0xee, sizeof c);
    part = COM_CALL(ID3D10Blob, GetBufferPointer, blob);
    T_TRUE(!(part >= c && part < c + sizeof c));
    T_TRUE(!memcmp(part, expected, 120));
    T_EQ(COM_CALL(ID3D10Blob, Release, blob), 0);
    size = t_dxbc_container(c, chunks, 4);

    /* The stops. */
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, NULL, size, 0, 0, &blob), "D3DGetBlobPart with no source data");
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, c, 0, 0, 0, &blob), "D3DGetBlobPart with no source data");
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, c, size, 0, 0, NULL), "D3DGetBlobPart with a NULL out-pointer");
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, c, size, 1, 0, &blob), "D3DGetBlobPart for a part other than the input signature (D3D_BLOB_PART 1)");
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, c, size, 4, 0, &blob), "D3DGetBlobPart for a part other than the input signature (D3D_BLOB_PART 4)");
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, c, size, 0, 1, &blob), "D3DGetBlobPart with flags 0x1");
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, c, 31, 0, 0, &blob), "D3DGetBlobPart of a container that does not parse");
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, c, 31, 0, 0, &blob), "31 bytes: shorter than the 32-byte header");
    memcpy(m, c, size); m[size - 1] ^= 0x40;
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, m, size, 0, 0, &blob), "these are not the bytes the compiler wrote");
    memcpy(m, c, size); t_dxbc_put32(m + 36, size); t_dxbc_stamp(m, size);          /* chunk 1's offset: past the end */
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, m, size, 0, 0, &blob), "chunk 1 at offset");
    const TDxbcChunk none[] = { { "RDEF", filler, 40 }, { "SHDR", filler, 33 } };
    uint32_t nsize = t_dxbc_container(m, none, 2);
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, m, nsize, 0, 0, &blob), "it has 0 ISGN chunks (and 0 ISG1) among its 2 chunks");
    const TDxbcChunk two[] = { { "ISGN", isgn, 76 }, { "ISGN", isgn, 76 } };
    nsize = t_dxbc_container(m, two, 2);
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, m, nsize, 0, 0, &blob), "it has 2 ISGN chunks");
    const TDxbcChunk isg1[] = { { "ISG1", wide, t_dxbc_signature(wide, true, one, 1) }, { "SHDR", filler, 8 } };
    nsize = t_dxbc_container(m, isg1, 2);
    T_TRAPS(CALL(d3dcompiler_46, D3DGetBlobPart, m, nsize, 0, 0, &blob), "it has 0 ISGN chunks (and 1 ISG1)");
}
