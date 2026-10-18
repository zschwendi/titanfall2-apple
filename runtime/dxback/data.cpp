// data.cpp -- the data group of the DXMT back end's table (unit dxback-split).  Entries that belong here:
//   CopyResource, CopySubresourceRegion, ResolveSubresource, UpdateSubresource, Map, Unmap, Begin, End, GetData
// Only this file fills them; dxback.cpp calls dxback_fill_data from build_table.
#include "dxback_priv.h"

// ---- the data group: each entry calls DXMT's method of the same name; the front has made every check ----
static void b_CopyResource(void *context, void *dst, void *src) {
    static_cast<ID3D11DeviceContext *>(context)->CopyResource(static_cast<ID3D11Resource *>(dst), static_cast<ID3D11Resource *>(src));
}

static void b_CopySubresourceRegion(void *context, void *dst, UINT dst_sub, UINT x, UINT y, UINT z, void *src, UINT src_sub, const D3D11_BOX *box) {
    static_cast<ID3D11DeviceContext *>(context)->CopySubresourceRegion(static_cast<ID3D11Resource *>(dst), dst_sub, x, y, z,
                                                                       static_cast<ID3D11Resource *>(src), src_sub, box);
}

static void b_ResolveSubresource(void *context, void *dst, UINT dst_sub, void *src, UINT src_sub, DXGI_FORMAT format) {
    static_cast<ID3D11DeviceContext *>(context)->ResolveSubresource(static_cast<ID3D11Resource *>(dst), dst_sub,
                                                                    static_cast<ID3D11Resource *>(src), src_sub, format);
}

static void b_UpdateSubresource(void *context, void *dst, UINT dst_sub, const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch) {
    static_cast<ID3D11DeviceContext *>(context)->UpdateSubresource(static_cast<ID3D11Resource *>(dst), dst_sub, box, data, row_pitch, depth_pitch);
}

static HRESULT b_Map(void *context, void *resource, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped) {
    return static_cast<ID3D11DeviceContext *>(context)->Map(static_cast<ID3D11Resource *>(resource), sub, type, flags, mapped);
}

static void b_Unmap(void *context, void *resource, UINT sub) {
    static_cast<ID3D11DeviceContext *>(context)->Unmap(static_cast<ID3D11Resource *>(resource), sub);
}

static void b_Begin(void *context, void *query) {
    static_cast<ID3D11DeviceContext *>(context)->Begin(static_cast<ID3D11Asynchronous *>(query));
}

static void b_End(void *context, void *query) {
    static_cast<ID3D11DeviceContext *>(context)->End(static_cast<ID3D11Asynchronous *>(query));
}

static HRESULT b_GetData(void *context, void *query, void *data, UINT size, UINT flags) {
    return static_cast<ID3D11DeviceContext *>(context)->GetData(static_cast<ID3D11Asynchronous *>(query), data, size, flags);
}

void dxback_fill_data(RtD3dBack &t) {
    t.CopyResource = b_CopyResource;
    t.CopySubresourceRegion = b_CopySubresourceRegion;
    t.ResolveSubresource = b_ResolveSubresource;
    t.UpdateSubresource = b_UpdateSubresource;
    t.Map = b_Map;
    t.Unmap = b_Unmap;
    t.Begin = b_Begin;
    t.End = b_End;
    t.GetData = b_GetData;
}
