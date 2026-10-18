/* Expected values: Microsoft's page of the creator (an equal state gives the same interface)
 * and Wine's d3d11 conformance tests as the contract's check reads them (docs/reviews/
 * d3d-contract-check.md, E5): what Windows normalises before it compares, and that the second
 * create returns the same object with one more reference.  The counts: a new device has 2
 * references and each object it makes holds it (docs/handoff/d3d-contract.md, section 3).
 * Unit d3d-fix-2, a target that does not blend: Wine's test_create_blend_state (dlls/d3d11/tests/
 * d3d11.c, read 2026-10-10) creates a zeroed description with only RenderTarget[0]'s write mask
 * set and reads back, for all 8 targets, FALSE, ONE (2), ZERO (1), ADD (1), ONE, ZERO, ADD and
 * the mask; Microsoft's page of D3D11_BLEND_DESC: with IndependentBlendEnable FALSE "only the
 * RenderTarget[0] members are used".  The description of the recording's call 7088 is
 * docs/trace/d3d-creators.md's row (IndependentBlendEnable TRUE; target 0 off/15, targets 1
 * and 2 ONE, ZERO; ONE, ZERO /15, target 3 off/15, targets 4 to 7 off/0), which the real device
 * took with S_OK; the stray numbers put where it does not blend are this test's own.  That an
 * alpha factor ending in _COLOR is not allowed is Microsoft's page of
 * D3D11_RENDER_TARGET_BLEND_DESC. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CreateBlendState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_BLEND_DESC d, same, bad;
    ID3D11BlendState *a = NULL, *b = NULL, *c = NULL;
    memset(&d, 0xcc, sizeof d);                         /* padding bytes of the game's stack must not matter */
    d.AlphaToCoverageEnable = FALSE;
    d.IndependentBlendEnable = FALSE;
    for (int i = 0; i < 8; i++)                         /* SRC_ALPHA 5, INV_SRC_ALPHA 6, ADD 1, ONE 2, ZERO 1, write mask ALL */
        d.RenderTarget[i] = (D3D11_RENDER_TARGET_BLEND_DESC){ TRUE, 5, 6, 1, 2, 1, 1, 0x0f };

    T_CASE("a new blend state: S_OK, one reference, the device held, the normalised description kept");
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &d, &a), S_OK);
    T_REQUIRE(a != NULL);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(device), 3);
    const NdStateObject *s = rt_com_state_of(a, &rt_nulldev_blend_class);
    T_TRUE(s->child.device == device);
    T_EQ(s->desc.blend.RenderTarget[0].SrcBlend, 5);
    T_EQ(s->desc.blend.RenderTarget[7].DestBlend, 6);
    T_EQ(s->desc.blend.RenderTarget[0].RenderTargetWriteMask, 0x0f);

    T_CASE("an equal description gives the same object with one more reference");
    memset(&same, 0x33, sizeof same);                   /* other padding */
    same.AlphaToCoverageEnable = FALSE;
    same.IndependentBlendEnable = FALSE;
    for (int i = 0; i < 8; i++) same.RenderTarget[i] = (D3D11_RENDER_TARGET_BLEND_DESC){ TRUE, 5, 6, 1, 2, 1, 1, 0x0f };
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &same, &b), S_OK);
    T_TRUE(b == a);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("with IndependentBlendEnable FALSE a description differing only in RenderTarget[3] is the same object");
    same.RenderTarget[3] = (D3D11_RENDER_TARGET_BLEND_DESC){ FALSE, 99, 99, 99, 99, 99, 99, 0xff };
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &same, &b), S_OK);
    T_TRUE(b == a);
    T_EQ(rt_com_refs(a), 3);
    T_EQ(s->desc.blend.RenderTarget[3].SrcBlend, 5);    /* render targets 1 to 7 are copies of 0 */

    T_CASE("a different description gives another object");
    same.RenderTarget[0].DestBlend = 2;
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &same, &c), S_OK);
    T_TRUE(c != a);
    T_EQ(rt_com_refs(c), 1);
    T_EQ(rt_com_refs(device), 4);

    T_CASE("after the last Release an equal description gives a new object");
    uint64_t serial = rt_com_serial(c);
    T_EQ(rt_com_release(c), 0);
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &same, &c), S_OK);
    T_TRUE(rt_com_serial(c) != serial);

    T_CASE("a zeroed description with one write mask set (Wine's): S_OK, and all 8 targets hold FALSE, ONE, ZERO, ADD, ONE, ZERO, ADD and the mask");
    D3D11_BLEND_DESC z;
    ID3D11BlendState *zs = NULL;
    memset(&z, 0, sizeof z);
    z.RenderTarget[0].RenderTargetWriteMask = 0x0f;
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &z, &zs), S_OK);
    T_REQUIRE(zs != NULL);
    const NdStateObject *zr = rt_com_state_of(zs, &rt_nulldev_blend_class);
    for (int i = 0; i < 8; i++) {
        const D3D11_RENDER_TARGET_BLEND_DESC *t = &zr->desc.blend.RenderTarget[i];
        T_EQ(t->BlendEnable, FALSE);
        T_EQ(t->SrcBlend, 2);
        T_EQ(t->DestBlend, 1);
        T_EQ(t->BlendOp, 1);
        T_EQ(t->SrcBlendAlpha, 2);
        T_EQ(t->DestBlendAlpha, 1);
        T_EQ(t->BlendOpAlpha, 1);
        T_EQ(t->RenderTargetWriteMask, 0x0f);
    }

    T_CASE("the recording's call 7088, stray numbers in the targets that do not blend: S_OK, those targets hold the defaults and their own mask");
    D3D11_BLEND_DESC r, r2;
    ID3D11BlendState *rs = NULL, *rs2 = NULL;
    memset(&r, 0xcc, sizeof r);
    r.AlphaToCoverageEnable = FALSE;
    r.IndependentBlendEnable = TRUE;
    r.RenderTarget[0] = (D3D11_RENDER_TARGET_BLEND_DESC){ FALSE, 0, 0, 0, 0, 0, 0, 0x0f };
    r.RenderTarget[1] = (D3D11_RENDER_TARGET_BLEND_DESC){ TRUE, 2, 1, 1, 2, 1, 1, 0x0f };
    r.RenderTarget[2] = (D3D11_RENDER_TARGET_BLEND_DESC){ TRUE, 2, 1, 1, 2, 1, 1, 0x0f };
    r.RenderTarget[3] = (D3D11_RENDER_TARGET_BLEND_DESC){ FALSE, 0x7fff0001, 99, 12, 0xcccccccc, 13, 6, 0x0f };
    for (int i = 4; i < 8; i++) r.RenderTarget[i] = (D3D11_RENDER_TARGET_BLEND_DESC){ FALSE, 0xcccccccc, 0, 20, 0, 0xffffffff, 0, 0 };
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &r, &rs), S_OK);
    T_REQUIRE(rs != NULL);
    const NdStateObject *rr = rt_com_state_of(rs, &rt_nulldev_blend_class);
    T_EQ(rr->desc.blend.IndependentBlendEnable, TRUE);
    for (int i = 0; i < 8; i++) {
        const D3D11_RENDER_TARGET_BLEND_DESC *t = &rr->desc.blend.RenderTarget[i];
        T_EQ(t->BlendEnable, i == 1 || i == 2);
        T_EQ(t->SrcBlend, 2);
        T_EQ(t->DestBlend, 1);
        T_EQ(t->BlendOp, 1);
        T_EQ(t->SrcBlendAlpha, 2);
        T_EQ(t->DestBlendAlpha, 1);
        T_EQ(t->BlendOpAlpha, 1);
        T_EQ(t->RenderTargetWriteMask, i < 4 ? 0x0f : 0);
    }

    T_CASE("descriptions that differ only in the unused fields of a target that does not blend are the same object, and a different mask there is another");
    r2 = r;
    r2.RenderTarget[3].SrcBlend = 7; r2.RenderTarget[3].BlendOpAlpha = 0x12345678; r2.RenderTarget[6].DestBlend = 3;
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &r2, &rs2), S_OK);
    T_TRUE(rs2 == rs);
    T_EQ(rt_com_refs(rs), 2);
    r2.RenderTarget[6].RenderTargetWriteMask = 0x08;
    T_EQ(COM_CALL(ID3D11Device, CreateBlendState, device, &r2, &rs2), S_OK);
    T_TRUE(rs2 != rs);

    T_TRAPS(COM_CALL(ID3D11Device, CreateBlendState, device, NULL, &b), "a state creator with a NULL description or a NULL out-pointer");
    bad = d; bad.RenderTarget[0].SrcBlend = 20;
    T_TRAPS(COM_CALL(ID3D11Device, CreateBlendState, device, &bad, &b), "a blend factor that is not one of D3D11_BLEND");
    bad = r; bad.RenderTarget[2].DestBlend = 12;            /* 12 and 13 are no D3D11_BLEND; the target blends */
    T_TRAPS(COM_CALL(ID3D11Device, CreateBlendState, device, &bad, &b), "a blend factor that is not one of D3D11_BLEND in render target 2");
    bad = r; bad.RenderTarget[1].SrcBlendAlpha = 3;         /* SRC_COLOR for alpha */
    T_TRAPS(COM_CALL(ID3D11Device, CreateBlendState, device, &bad, &b), "an alpha blend factor that is not one of D3D11_BLEND or ends in _COLOR");
    bad = r; bad.RenderTarget[2].BlendOp = 6;
    T_TRAPS(COM_CALL(ID3D11Device, CreateBlendState, device, &bad, &b), "a blend operation that is not one of D3D11_BLEND_OP");
    bad = r; bad.RenderTarget[5].RenderTargetWriteMask = 0x10;  /* checked whether or not the target blends */
    T_TRAPS(COM_CALL(ID3D11Device, CreateBlendState, device, &bad, &b), "a write mask above D3D11_COLOR_WRITE_ENABLE_ALL in render target 5");
}
