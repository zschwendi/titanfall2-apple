/* Expected values, none from the shim.
 *   The containers are assembled by rt/test_dxbc.h from the documented layout (docs/research/
 *   dxbc-shaders.md, 1.1 and 1.4); the names and numbers asked back are the ones the test put in.
 *   S_OK for a container that parses, and the interface ID: the game's call (materialsystem_dx11
 *   +0x323d8 passes the ID image at +0x1da8f0, which tools/scout/d3d11_iids.py names
 *   ID3D11ShaderReflection, and goes on as if the call had succeeded); the ID's bytes are
 *   written out here from MinGW's d3d11shader.h (the branch for D3D_COMPILER_VERSION above 43).
 *   One reference: COM's rule for an object handed out (from memory).
 *   The stops: the shim's own decisions (its comment says why each is a stop and not a code). */
#include <string.h>

#include "rt/test.h"
#include "rt/test_dxbc.h"
#include "rt/nulldev.h"

static const TDxbcElement sig[] = {
    { "POSITION", 0, 0, 3, 0, 0x7, 0x7, 0, 0 },
    { "TEXCOORD", 1, 0, 3, 1, 0x3, 0x3, 0, 2 },
};

/* {8d536ca1-0cca-4956-a837-786963755584} as it lies in memory, and d3dcompiler_43's
 * {0a233719-3960-4578-9d7c-203b8b1d9cc1}, which this library version does not have. */
static const uint8_t iid_46[16] = { 0xa1, 0x6c, 0x53, 0x8d, 0xca, 0x0c, 0x56, 0x49, 0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84 };
static const uint8_t iid_43[16] = { 0x19, 0x37, 0x23, 0x0a, 0x60, 0x39, 0x78, 0x45, 0x9d, 0x7c, 0x20, 0x3b, 0x8b, 0x1d, 0x9c, 0xc1 };

TEST(d3dcompiler_46, D3DReflect)
{
    uint8_t isgn[T_DXBC_MAX], isg1[T_DXBC_MAX], c[T_DXBC_MAX], m[T_DXBC_MAX], filler[24];
    D3D11_SIGNATURE_PARAMETER_DESC desc;
    void *out = (void *)(uintptr_t)0x0ed0c101;                      /* the game's slot holds leftovers */
    REFIID iid = (REFIID)iid_46;

    memset(filler, 0x3c, sizeof filler);
    uint32_t isgn_len = t_dxbc_signature(isgn, false, sig, 2), isg1_len = t_dxbc_signature(isg1, true, sig, 2);
    const TDxbcChunk chunks[] = { { "RDEF", filler, 24 }, { "ISGN", isgn, isgn_len }, { "OSGN", isgn, isgn_len }, { "SHEX", filler, 20 } };
    uint32_t size = t_dxbc_container(c, chunks, 4);

    T_CASE("a container with an input signature, asked for ID3D11ShaderReflection: S_OK and a reflection object with one reference");
    T_EQ(CALL(d3dcompiler_46, D3DReflect, c, size, iid, &out), S_OK);
    ID3D11ShaderReflection *r = out;
    T_REQUIRE(r != NULL && rt_com_class_of(r) == &rt_nulldev_reflection_class);
    T_EQ(LD64((uint64_t)(uintptr_t)r), rt_iface_ID3D11ShaderReflection.vtable);     /* what lifted code reads at offset 0 */
    T_EQ(rt_com_refs(r), 1);
    T_TRUE(!memcmp(iid_46, &rt_iface_ID3D11ShaderReflection.iid, 16));              /* the description's ID is that library version's */
    NdReflection *state = rt_com_state_of(r, &rt_nulldev_reflection_class);
    T_EQ(state->ninputs, 2);
    T_EQ(state->bytecode_length, size);
    T_EQ(COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, r, 1, &desc), S_OK);
    T_TRUE(!strcmp(desc.SemanticName, "TEXCOORD"));
    T_EQ(desc.SemanticIndex, 1);
    /* A second call makes a second object. */
    void *out2 = NULL;
    T_EQ(CALL(d3dcompiler_46, D3DReflect, c, size, iid, &out2), S_OK);
    T_TRUE(out2 != NULL && out2 != out);
    T_EQ(COM_CALL(ID3D11ShaderReflection, Release, out2), 0);

    T_CASE("the caller's bytes are not kept");
    memset(c, 0xee, size);
    memset(&desc, 0, sizeof desc);
    T_EQ(COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, r, 0, &desc), S_OK);
    T_TRUE(!((const uint8_t *)desc.SemanticName >= c && (const uint8_t *)desc.SemanticName < c + sizeof c));
    T_TRUE(!strcmp(desc.SemanticName, "POSITION"));
    T_EQ(desc.Mask, 0x7);
    T_EQ(COM_CALL(ID3D11ShaderReflection, Release, r), 0);
    size = t_dxbc_container(c, chunks, 4);

    T_CASE("an ISG1 signature is read as well");
    const TDxbcChunk wide[] = { { "ISG1", isg1, isg1_len }, { "SHEX", filler, 20 } };
    uint32_t wsize = t_dxbc_container(m, wide, 2);
    out = NULL;
    T_EQ(CALL(d3dcompiler_46, D3DReflect, m, wsize, iid, &out), S_OK);
    T_REQUIRE(out != NULL);
    T_EQ(COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, out, 1, &desc), S_OK);
    T_TRUE(!strcmp(desc.SemanticName, "TEXCOORD"));
    T_EQ(desc.MinPrecision, 2);
    T_EQ(COM_CALL(ID3D11ShaderReflection, Release, out), 0);

    /* The stops. */
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, NULL, size, iid, &out), "D3DReflect with no source data");
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, c, 0, iid, &out), "D3DReflect with no source data");
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, c, size, NULL, &out), "D3DReflect with a NULL interface ID or out-pointer");
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, c, size, iid, NULL), "D3DReflect with a NULL interface ID or out-pointer");
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, c, size, (REFIID)iid_43, &out),
            "D3DReflect for an interface other than d3dcompiler_46's ID3D11ShaderReflection: {0a233719-3960-4578-9d7c-203b8b1d9cc1}");
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, c, size, &rt_iface_ID3D11Device.iid, &out), "D3DReflect for an interface other than");

    /* A container that does not parse: the stop says which check failed (tests/core/dxbc.c has every check). */
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, c, 31, iid, &out), "D3DReflect of a container that does not parse");
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, c, 31, iid, &out), "31 bytes: shorter than the 32-byte header");
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, c, size - 4, iid, &out), "the header declares");
    memcpy(m, c, size); m[size - 2] ^= 0x10;
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, m, size, iid, &out), "these are not the bytes the compiler wrote");
    /* A chunk offset past the end (chunk 3's offset is the table's fourth dword, at 44). */
    memcpy(m, c, size); t_dxbc_put32(m + 44, size + 4); t_dxbc_stamp(m, size);
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, m, size, iid, &out), "chunk 3 at offset");
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, m, size, iid, &out), "its 8-byte header runs past the container's");
    /* The ISGN chunk is chunk 1: its header at 48 + 8 + 24 = 80, its data at 88. */
    T_EQ(memcmp(c + 80, "ISGN", 4), 0);
    /* A name offset past the chunk (element 1's name offset is the first dword of the second element). */
    memcpy(m, c, size); t_dxbc_put32(m + 88 + 8 + 24, isgn_len + 100); t_dxbc_stamp(m, size);
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, m, size, iid, &out), "ISGN element 1: its name at offset");
    /* A count that does not fit. */
    memcpy(m, c, size); t_dxbc_put32(m + 88, 3); t_dxbc_stamp(m, size);
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, m, size, iid, &out), "the ISGN chunk's 3 elements of 24 bytes at offset 8 do not fit");
    /* No input signature, and two. */
    const TDxbcChunk none[] = { { "RDEF", filler, 24 }, { "OSGN", isgn, isgn_len }, { "SHEX", filler, 20 } };
    uint32_t nsize = t_dxbc_container(m, none, 3);
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, m, nsize, iid, &out), "it has 0 input-signature chunks (ISGN or ISG1) among its 3 chunks");
    const TDxbcChunk two[] = { { "ISGN", isgn, isgn_len }, { "ISG1", isg1, isg1_len } };
    nsize = t_dxbc_container(m, two, 2);
    T_TRAPS(CALL(d3dcompiler_46, D3DReflect, m, nsize, iid, &out), "it has 2 input-signature chunks");
}
