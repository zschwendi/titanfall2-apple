/* The ledger of invented answers and the run switches (rt/nulldev.h; runtime/nulldev/ledger.c;
 * unit d3d-fix-2, items 6 and G2).
 *
 * Expected values: the planner's decisions in the unit's brief (occlusion 0 by default; with
 * the switch, the viewport's area times the sample count when a draw call came between Begin
 * and End, else 0; a read-back only a renderer could have produced is a stop by default and
 * zeroes with the switch, counted; timestamps from the host's monotonic clock with a stated
 * frequency and "not disjoint").  640 * 480 = 307200; times 4 samples 1228800.  The frequency
 * 10,000,000 is ND_TICKS_PER_SECOND (100 ns units, rt/time.h). */
#include <stdlib.h>
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

static UINT64 occlusion(ID3D11DeviceContext *context, ID3D11Query *q, bool draw) {
    UINT64 n = 0xdeadbeef;
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q);
    if (draw) COM_CALL(ID3D11DeviceContext, Draw, context, 3, 0);
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q, &n, 8, 0), S_OK);
    return n;
}

TEST(core, nulldev_ledger)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    unsetenv("RT_D3D_OCCLUSION"); unsetenv("RT_D3D_READBACK");
    rt_nulldev_switches_reset();
    D3D11_QUERY_DESC qd = { D3D11_QUERY_OCCLUSION, 0 };
    ID3D11Query *occ = NULL, *stamp = NULL, *dis = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &qd, &occ), S_OK);
    c->nviewports = 1;
    c->viewport[0] = (D3D11_VIEWPORT){ 0, 0, 640, 480, 0, 1 };

    T_CASE("by default an occlusion query answers 0 with or without a draw, and the ledger counts each answer");
    uint64_t before = rt_nulldev_invented_count(ND_INV_OCCLUSION);
    T_EQ(occlusion(context, occ, true), 0);
    T_EQ(occlusion(context, occ, false), 0);
    T_EQ(rt_nulldev_invented_count(ND_INV_OCCLUSION), before + 2);
    if (!before) T_TRUE(T_LOGGED(RT_CH_COM, "first invented answer of its kind (occlusion"));

    T_CASE("RT_D3D_OCCLUSION=all: the viewport's area times the sample count when a draw came between Begin and End, else 0");
    setenv("RT_D3D_OCCLUSION", "all", 1);
    rt_nulldev_switches_reset();
    T_EQ(occlusion(context, occ, true), 307200);
    T_EQ(occlusion(context, occ, false), 0);
    D3D11_TEXTURE2D_DESC ms = { 640, 480, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 4, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    D3D11_RENDER_TARGET_VIEW_DESC vd = { DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2DMS, { { 0 } } };
    ID3D11Texture2D *target = NULL;
    ID3D11RenderTargetView *view = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &ms, NULL, &target), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)target, &vd, &view), S_OK);
    rt_nulldev_bind(&c->rtv[0], view);
    T_EQ(occlusion(context, occ, true), 1228800);
    rt_nulldev_bind(&c->rtv[0], NULL);
    unsetenv("RT_D3D_OCCLUSION");
    rt_nulldev_switches_reset();
    T_EQ(occlusion(context, occ, true), 0);

    T_CASE("timestamps rise, the disjoint answer is 10,000,000 a second and not disjoint, and both are counted");
    UINT64 t0 = 0, t1 = 0;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
    memset(&dj, 0xa5, sizeof dj);
    qd.Query = D3D11_QUERY_TIMESTAMP;
    T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &qd, &stamp), S_OK);
    qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &qd, &dis), S_OK);
    uint64_t stamps = rt_nulldev_invented_count(ND_INV_TIMESTAMP), disjoints = rt_nulldev_invented_count(ND_INV_DISJOINT);
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)dis);
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)stamp);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)stamp, &t0, 8, 0), S_OK);
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)stamp);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)stamp, &t1, 8, 0), S_OK);
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)dis);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)dis, &dj, 16, 0), S_OK);
    T_TRUE(t0 != 0 && t1 >= t0);
    T_EQ(dj.Frequency, 10000000);
    T_EQ(dj.Disjoint, FALSE);
    T_EQ(rt_nulldev_invented_count(ND_INV_TIMESTAMP), stamps + 2);
    T_EQ(rt_nulldev_invented_count(ND_INV_DISJOINT), disjoints + 1);

    T_CASE("RT_D3D_READBACK=zero: the recording's read-back is handed out as zeroes and counted; without the switch it is a stop");
    D3D11_TEXTURE2D_DESC rt = { 32, 32, 1, 1, DXGI_FORMAT_R32_TYPELESS, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0 };
    D3D11_TEXTURE2D_DESC rb = { 32, 32, 1, 1, DXGI_FORMAT_R32_FLOAT, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
    ID3D11Texture2D *drawn = NULL, *back = NULL;
    D3D11_MAPPED_SUBRESOURCE m;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &rt, NULL, &drawn), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &rb, NULL, &back), S_OK);
    memset(rt_nulldev_block(back, 0)->data, 0x5a, 32 * 32 * 4);         /* stale bytes must not come back */
    COM_CALL(ID3D11DeviceContext, CopyResource, context, (ID3D11Resource *)back, (ID3D11Resource *)drawn);
    uint64_t reads = rt_nulldev_invented_count(ND_INV_READBACK);
    setenv("RT_D3D_READBACK", "zero", 1);
    rt_nulldev_switches_reset();
    T_EQ(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)back, 0, D3D11_MAP_READ, 0, &m), S_OK);
    T_EQ(m.RowPitch, 128);
    const uint8_t *p = m.pData;
    unsigned nonzero = 0;
    for (unsigned i = 0; i < 32 * 32 * 4; i++) nonzero += p[i] != 0;
    T_EQ(nonzero, 0);
    T_EQ(rt_nulldev_invented_count(ND_INV_READBACK), reads + 1);
    COM_CALL(ID3D11DeviceContext, Unmap, context, (ID3D11Resource *)back, 0);
    unsetenv("RT_D3D_READBACK");
    rt_nulldev_switches_reset();
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Map, context, (ID3D11Resource *)back, 0, D3D11_MAP_READ, 0, &m), "RT_D3D_READBACK=zero hands out zeroes instead");
}
