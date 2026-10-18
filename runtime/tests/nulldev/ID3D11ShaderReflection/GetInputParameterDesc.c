/* Expected values, none from the body.
 *   The signature is the test's own: five elements with numbers chosen so that no two fields
 *   of an element are equal, put into a container by rt/test_dxbc.h from the documented layout
 *   (docs/research/dxbc-shaders.md, 1.4), once as ISGN and once as ISG1.  What comes back must
 *   be those numbers, each at the offset MinGW's d3d11shader.h gives the field for
 *   D3D_COMPILER_VERSION 46 (40 bytes: name 0, index 8, register 12, system value 16,
 *   component type 20, mask 24, read mask 25, stream 28, minimum precision 32); the offsets
 *   are written here as numbers and read from a raw buffer, not through our structure.
 *   Stream and minimum precision of an ISGN element are 0: the element has neither (research
 *   3.5; Wine's test expects Windows to write MinPrecision 0 for such a shader from library
 *   version 46 on).
 *   E_INVALIDARG (0x80070057) for an index past the end and for a NULL description: Wine's
 *   tests of the interface expect it from Windows (dlls/d3dcompiler_43/tests/reflection.c,
 *   read 2026-10-11), and the game's loop ends on that value only (materialsystem_dx11+0x32635).
 *   That a refused call writes nothing, and that the padding bytes are left alone, is this
 *   device's own choice (the body's comment); the test pins it. */
#include <string.h>

#include "rt/test.h"
#include "rt/test_dxbc.h"
#include "rt/nulldev.h"

static const TDxbcElement sig[] = {
    /* name          index  system value  type  register  mask  read  stream  precision */
    { "POSITION",    0,     0,            3,    0,        0x7,  0x3,  0,      1 },
    { "BLENDWEIGHT", 0,     0,            2,    1,        0x3,  0x1,  0,      4 },
    { "TEXCOORD",    5,     0,            3,    2,        0xf,  0xd,  0,      0 },   /* an index above 0 */
    { "SV_InstanceID", 0,   8,            1,    3,        0x1,  0x0,  0,      5 },   /* a system value: D3D_NAME_INSTANCE_ID */
    { "COLOR",       1,     0,            1,    4,        0xb,  0xa,  2,      2 },   /* masks that are not a run from x */
};
#define NSIG 5u

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static ID3D11ShaderReflection *reflect(bool wide) {
    uint8_t s[T_DXBC_MAX], c[T_DXBC_MAX], filler[16];
    char why[RT_DXBC_WHY] = "";
    memset(filler, 0x77, sizeof filler);
    uint32_t len = t_dxbc_signature(s, wide, sig, NSIG);
    const TDxbcChunk chunks[] = { { wide ? "ISG1" : "ISGN", s, len }, { "SHEX", filler, 16 } };
    uint32_t size = t_dxbc_container(c, chunks, 2);
    ID3D11ShaderReflection *r = rt_nulldev_reflection_new(c, size, why);
    if (!r) rt_test_fail(__FILE__, __LINE__, "the test's container was refused: %s", why);
    memset(c, 0xdd, sizeof c);                  /* the object keeps nothing of the caller's bytes */
    return r;
}

TEST(ID3D11ShaderReflection, GetInputParameterDesc)
{
    ID3D11ShaderReflection *narrow = reflect(false), *wide = reflect(true);
    T_REQUIRE(narrow != NULL && wide != NULL);
    uint8_t raw[40 + 8];

    T_CASE("each element of the signature, field by field, S_OK");
    for (int form = 0; form < 2; form++) {
        ID3D11ShaderReflection *r = form ? wide : narrow;
        const char *names[NSIG];
        for (uint32_t i = 0; i < NSIG; i++) {
            D3D11_SIGNATURE_PARAMETER_DESC d;
            memset(&d, 0xa5, sizeof d);
            T_EQ(COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, r, i, &d), S_OK);
            T_REQUIRE(d.SemanticName != NULL);
            T_TRUE(!strcmp(d.SemanticName, sig[i].name));
            T_EQ(d.SemanticIndex, sig[i].index);
            T_EQ(d.Register, sig[i].reg);
            T_EQ(d.SystemValueType, sig[i].system_value);
            T_EQ(d.ComponentType, sig[i].component_type);
            T_EQ(d.Mask, sig[i].mask);
            T_EQ(d.ReadWriteMask, sig[i].rw_mask);
            T_EQ(d.Stream, form ? sig[i].stream : 0);
            T_EQ(d.MinPrecision, form ? sig[i].min_precision : 0);
            names[i] = d.SemanticName;
        }
        /* The names stay where they were while the object lives: a second pass gives the same pointers and text. */
        for (uint32_t i = 0; i < NSIG; i++) {
            D3D11_SIGNATURE_PARAMETER_DESC d;
            T_EQ(COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, r, i, &d), S_OK);
            T_TRUE(d.SemanticName == names[i]);
            T_TRUE(!strcmp(names[i], sig[i].name));
        }
    }

    T_CASE("the 40 bytes of d3dcompiler_46's structure and nothing after them");
    memset(raw, 0xa5, sizeof raw);
    T_EQ(COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, wide, 4, (D3D11_SIGNATURE_PARAMETER_DESC *)raw), S_OK);
    const char *name;
    memcpy(&name, raw, 8);
    T_TRUE(!strcmp(name, "COLOR"));
    T_EQ(rd32(raw + 8), 1);                     /* SemanticIndex */
    T_EQ(rd32(raw + 12), 4);                    /* Register */
    T_EQ(rd32(raw + 16), 0);                    /* SystemValueType */
    T_EQ(rd32(raw + 20), 1);                    /* ComponentType */
    T_EQ(raw[24], 0xb);                         /* Mask */
    T_EQ(raw[25], 0xa);                         /* ReadWriteMask */
    T_EQ(raw[26], 0xa5);                        /* padding: left alone */
    T_EQ(raw[27], 0xa5);
    T_EQ(rd32(raw + 28), 2);                    /* Stream */
    T_EQ(rd32(raw + 32), 2);                    /* MinPrecision */
    for (int i = 36; i < 48; i++) T_EQ(raw[i], 0xa5);     /* the structure's tail padding, and what lies behind it */
    memset(raw, 0xa5, sizeof raw);
    T_EQ(COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, narrow, 3, (D3D11_SIGNATURE_PARAMETER_DESC *)raw), S_OK);
    memcpy(&name, raw, 8);
    T_TRUE(!strcmp(name, "SV_InstanceID"));
    T_EQ(rd32(raw + 12), 3);
    T_EQ(rd32(raw + 16), 8);
    T_EQ(raw[24], 0x1);
    T_EQ(raw[25], 0x0);
    T_EQ(rd32(raw + 28), 0);                    /* an ISGN element: stream 0 */
    T_EQ(rd32(raw + 32), 0);                    /* and MinPrecision written as 0 */
    for (int i = 36; i < 48; i++) T_EQ(raw[i], 0xa5);

    T_CASE("an index past the end is E_INVALIDARG and writes nothing");
    static const uint32_t past[] = { NSIG, NSIG + 1, 0x7fffffff, 0xffffffff };
    for (unsigned k = 0; k < sizeof past / sizeof past[0]; k++) {
        memset(raw, 0xa5, sizeof raw);
        T_EQ((uint32_t)COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, narrow, past[k], (D3D11_SIGNATURE_PARAMETER_DESC *)raw), 0x80070057u);
        for (unsigned i = 0; i < sizeof raw; i++) T_EQ(raw[i], 0xa5);
    }
    /* The loop the game runs: from 0 until that value; it sees every element once. */
    uint32_t seen = 0;
    D3D11_SIGNATURE_PARAMETER_DESC d;
    while ((uint32_t)COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, wide, seen, &d) != 0x80070057u && seen < 100) seen++;
    T_EQ(seen, NSIG);
    /* A signature with no element ends the loop at once. */
    uint8_t s[16], c[T_DXBC_MAX];
    char why[RT_DXBC_WHY];
    const TDxbcChunk empty[] = { { "ISGN", s, t_dxbc_signature(s, false, sig, 0) } };
    ID3D11ShaderReflection *none = rt_nulldev_reflection_new(c, t_dxbc_container(c, empty, 1), why);
    T_REQUIRE(none != NULL);
    T_EQ((uint32_t)COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, none, 0, &d), 0x80070057u);

    T_CASE("a NULL description is E_INVALIDARG");
    T_EQ((uint32_t)COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, narrow, 0, NULL), 0x80070057u);
    T_EQ((uint32_t)COM_CALL(ID3D11ShaderReflection, GetInputParameterDesc, narrow, NSIG, NULL), 0x80070057u);

    /* Release through the layer; the block of names goes with the object. */
    T_EQ(COM_CALL(ID3D11ShaderReflection, Release, narrow), 0);
    T_EQ(COM_CALL(ID3D11ShaderReflection, Release, wide), 0);
    T_EQ(COM_CALL(ID3D11ShaderReflection, Release, none), 0);
}
