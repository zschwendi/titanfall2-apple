// draw.cpp -- the draw group of the DXMT back end's table (unit dxback-split).  Entries that belong here:
//   Draw, DrawIndexed, DrawInstanced, DrawIndexedInstanced, DrawInstancedIndirect, Dispatch, DispatchIndirect, ClearRenderTargetView, ClearDepthStencilView, ClearUnorderedAccessViewFloat, ClearState, Flush
// Only this file fills them; dxback.cpp calls dxback_fill_draw from build_table.
//
// Each entry casts the back objects to DXMT's interfaces and calls DXMT's method of the same name.  The front has
// made every check; these bodies keep no state.
#include "dxback_priv.h"

static void b_Draw(void *context, UINT vertex_count, UINT start_vertex) {
    static_cast<ID3D11DeviceContext *>(context)->Draw(vertex_count, start_vertex);
}

static void b_DrawIndexed(void *context, UINT index_count, UINT start_index, INT base_vertex) {
    static_cast<ID3D11DeviceContext *>(context)->DrawIndexed(index_count, start_index, base_vertex);
}

static void b_DrawInstanced(void *context, UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance) {
    static_cast<ID3D11DeviceContext *>(context)->DrawInstanced(vertex_count, instance_count, start_vertex, start_instance);
}

static void b_DrawIndexedInstanced(void *context, UINT index_count, UINT instance_count, UINT start_index, INT base_vertex,
                                   UINT start_instance) {
    static_cast<ID3D11DeviceContext *>(context)->DrawIndexedInstanced(index_count, instance_count, start_index, base_vertex,
                                                                      start_instance);
}

static void b_DrawInstancedIndirect(void *context, void *buffer, UINT offset) {
    static_cast<ID3D11DeviceContext *>(context)->DrawInstancedIndirect(static_cast<ID3D11Buffer *>(buffer), offset);
}

static void b_Dispatch(void *context, UINT x, UINT y, UINT z) {
    static_cast<ID3D11DeviceContext *>(context)->Dispatch(x, y, z);
}

static void b_DispatchIndirect(void *context, void *buffer, UINT offset) {
    static_cast<ID3D11DeviceContext *>(context)->DispatchIndirect(static_cast<ID3D11Buffer *>(buffer), offset);
}

static void b_ClearRenderTargetView(void *context, void *view, const FLOAT *rgba) {
    static_cast<ID3D11DeviceContext *>(context)->ClearRenderTargetView(static_cast<ID3D11RenderTargetView *>(view), rgba);
}

static void b_ClearDepthStencilView(void *context, void *view, UINT flags, FLOAT depth, UINT8 stencil) {
    static_cast<ID3D11DeviceContext *>(context)->ClearDepthStencilView(static_cast<ID3D11DepthStencilView *>(view), flags,
                                                                       depth, stencil);
}

static void b_ClearUnorderedAccessViewFloat(void *context, void *view, const FLOAT *values) {
    static_cast<ID3D11DeviceContext *>(context)->ClearUnorderedAccessViewFloat(static_cast<ID3D11UnorderedAccessView *>(view),
                                                                               values);
}

static void b_ClearState(void *context) {
    static_cast<ID3D11DeviceContext *>(context)->ClearState();
}

static void b_Flush(void *context) {
    static_cast<ID3D11DeviceContext *>(context)->Flush();
}

void dxback_fill_draw(RtD3dBack &t) {
    t.Draw = b_Draw;
    t.DrawIndexed = b_DrawIndexed;
    t.DrawInstanced = b_DrawInstanced;
    t.DrawIndexedInstanced = b_DrawIndexedInstanced;
    t.DrawInstancedIndirect = b_DrawInstancedIndirect;
    t.Dispatch = b_Dispatch;
    t.DispatchIndirect = b_DispatchIndirect;
    t.ClearRenderTargetView = b_ClearRenderTargetView;
    t.ClearDepthStencilView = b_ClearDepthStencilView;
    t.ClearUnorderedAccessViewFloat = b_ClearUnorderedAccessViewFloat;
    t.ClearState = b_ClearState;
    t.Flush = b_Flush;
}
