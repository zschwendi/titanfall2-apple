/* The seam behind the do-nothing Direct3D device: the BACK-END INTERFACE (unit pic-seam;
 * docs/handoff/design-picture.md, sections 1.3, 1.4 and 7; docs/handoff/pic-seam.md).
 *
 * The do-nothing device (rt/nulldev.h) is the FRONT: it owns every object the game holds,
 * checks every call and answers every question.  A BACK END is a renderer, or something that
 * stands where one would: it is handed each call after the front accepted it.  The game never
 * holds a back-end pointer and a back end never holds a game object.  There are three:
 *
 *     none     the default.  No table: rt_d3dback() is NULL and the device is the one it was
 *              before this unit (guard pages, the ledger of invented answers, the stops).
 *     record   runtime/nulldev/back_record.c: counts every entry, keeps the last arguments,
 *              checks every object it is handed.  For tests and for `make d3d-replay-back`.
 *     dxmt     the native renderer, in runtime/dxback/ (a later unit).  Until it exists the
 *              selection stops with a report that says so.
 *
 * Selection: RT_D3D_BACKEND=none|record|dxmt in the environment, read when the first device is
 * made (rt_d3dback_select); a test or a tool chooses with rt_d3dback_use before that.
 *
 * THE RULES OF THE SEAM.  Each is kept by a test (runtime/tests/core/d3dback_*.c and
 * runtime/tests/back/), named in brackets.
 *
 *  1. AFTER THE CHECKS.  A body calls the back end only after everything the front checks has
 *     passed: never for a call the front stops on, never for one it answers with a failure
 *     (GetData before End, ResizeBuffers with a referenced back buffer).  [Every fatal report
 *     made while a back end is there ends with the line "d3dback: the call that stopped had
 *     not reached the back end", or with one that names the entry it had reached; a test
 *     proves the first with T_TRAPS for each stop.  core/d3dback_rules.]
 *  2. OBJECTS CROSS AS BACK POINTERS.  An object argument crosses as the back end's own object
 *     (rt_d3dback_of), an array of objects as an array of back pointers rebuilt on the stack
 *     (the game's array is never passed on), NULL as NULL, a NULL array as NULL.  A live front
 *     object of a class of ND_BACK_KINDS that has no back pointer while a back end is there is
 *     fatal in rt_d3dback_of.  Structures and data cross by pointer, unchanged: the game's own
 *     memory, valid for the call only.  [The recording back end refuses any object argument
 *     that is not a live object of its own of the right kind: core/d3dback_rules.]
 *  3. THE TWO ENDS OF A BACK OBJECT.  A creator makes the front object, then asks the back end
 *     for its own and keeps the pointer in the front record (NdChild.back, NdDevice.back,
 *     NdSwapChain.back).  The front object's destruction releases it: entry Release, once.
 *     The swap chain's back buffer is asked for with SwapChainGetBuffer whenever the front
 *     makes one (CreateSwapChain, ResizeBuffers); views of it are views like any other.
 *     [core/d3dback_objects: after everything is released the recording back end has no live
 *     object.]
 *  4. A DIFFERENT RESULT IS A STOP.  Where the front has the answer (a creator's S_OK,
 *     ResizeBuffers' S_OK) a result of the back end that differs, or success without the
 *     object, stops the run with a report that names the method and both results
 *     (rt_d3dback_differs).  [core/d3dback_rules.]
 *  5. MAP.  With a back end the game is handed the back end's pointer and pitches, and the
 *     front keeps no bytes for any resource (NdMemory.kept is 0: no block, no guard page; it
 *     keeps the description, the size and what is mapped).  The read mapping of contents only
 *     a renderer could have produced is not a stop then: the renderer has them.  With `none`
 *     nothing changes.  [back/ID3D11DeviceContext/Map.]
 *  6. THE INVENTED ANSWERS.  Occlusion, timestamp, disjoint, event (GetData), read-back (Map)
 *     and the present with its pacing (Present): with a back end the answer is the back end's
 *     and the ledger of invented answers (rt_nulldev_invented) counts nothing for it.  A
 *     failure is still a stop (rule 4): GetData may answer S_OK or S_FALSE, Present any success
 *     code, Map S_OK only.  With `none` nothing changes.  [back/.../GetData, Present, Map.]
 *  7. A GET IS THE FRONT'S.  A method that reads state back (the context's Get methods, GetDesc,
 *     GetResource, the swap chain's GetBuffer and GetDesc, the format queries, the adapter's
 *     and the output's answers) is answered by the front alone: the back end has no entry for
 *     it and is not asked.  So is a creator's answer when no new object is made (a state
 *     object that already exists, a call that only validates).  [back/.../VSGetConstantBuffers;
 *     core/d3dback_objects for the creators.]
 *  8. ONE THREAD IN THE CONTEXT AT A TIME, in every mode (rt/nulldev.h, NdGuard): the front
 *     records the thread of each call of ID3D11DeviceContext and of the swap chain's Present
 *     and ResizeBuffers, and a second thread that enters while one is inside is a stop.
 *     [core/d3dback_guard.]
 *
 * WHAT A BODY WRITES.  One of the macros below, once, on every path the front accepts:
 *     RT_BACK          a call that returns nothing
 *     RT_BACK_SAME     a call whose result the front already has (rule 4)
 *     RT_BACK_CREATE   a creator (rules 3, 4 and 7)
 *     RT_BACK_ALONE    a path on which the front answers alone on purpose (rules 1 and 7)
 *     RT_BACK_ANSWER   a call whose result is the back end's (rule 6); the body checks it
 * Each counts in the seam's own table (rt_d3dback_asked, rt_d3dback_alone), so that for every
 * method   calls the front did not stop on  ==  asked + answered alone   and, with the
 * recording back end,   asked == reached.   `make d3d-replay-back` prints exactly that.
 * With `none` a macro costs one load and one branch, and its arguments are not evaluated.
 *
 * THREADS.  A back end's entries are called on the guest thread that made the call, in the x86
 * ordering mode.  Creators and Release may come from any thread, several at once; the context's
 * and the swap chain's entries from one thread at a time (rule 8).
 *
 * THE ENTRIES: RT_BACK_N of them (75), one per method of the device's contract that a renderer
 * must see (docs/handoff/d3d-contract.md, section 7): the 18 creators and the frame latency of
 * the device, 50 of the context, 3 of the swap chain and its creator, and 3 of the seam's own
 * (CreateDevice, Release, SwapChainGetBuffer).  The first argument is the back end's device,
 * context or swap chain.  An entry is added HERE, in RT_D3DBACK_ENTRIES and in the recording
 * back end, when the front gains a method a renderer must see; a body file never edits this
 * header.
 */
#ifndef RT_D3DBACK_H
#define RT_D3DBACK_H

#include <stdatomic.h>

#ifndef RT_D3DBACK_OWN_TYPES
#include "rt/shim.h"
#include "rt/win/d3d11.h"
#include "com_types.h"          /* generated: the enumerations as the runtime names them (DXGI_FORMAT, D3D11_MAP, ...).
                                   A back end compiled against Direct3D's own headers includes those instead and
                                   defines RT_D3DBACK_OWN_TYPES first: the layouts are the same (the six WCHAR
                                   structures apart, none of which crosses) */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* What a back object is: the class of the front object it stands behind. */
typedef enum RtD3dBackKind {
    RT_BK_DEVICE, RT_BK_CONTEXT, RT_BK_SWAPCHAIN,
    RT_BK_BUFFER, RT_BK_TEXTURE1D, RT_BK_TEXTURE2D, RT_BK_TEXTURE3D,
    RT_BK_SRV, RT_BK_UAV, RT_BK_RTV, RT_BK_DSV,
    RT_BK_BLEND, RT_BK_DEPTHSTENCIL, RT_BK_RASTERIZER, RT_BK_SAMPLER,
    RT_BK_VS, RT_BK_PS, RT_BK_GS, RT_BK_CS,
    RT_BK_INPUTLAYOUT, RT_BK_QUERY,
    RT_BK_KINDS
} RtD3dBackKind;

/* The table.  Every member after `name` is one entry; all must be set.  A result is an
 * HRESULT as Direct3D gives it; `void **` receives a new back object with one reference, which
 * the front gives back with Release.  "resource" is a buffer or a texture of any dimension. */
typedef struct RtD3dBack {
    const char *name;                                   /* "record", "dxmt": for reports */

    /* ---- the seam's own ---- */
    /* What D3D11CreateDevice makes: hardware, no flags, feature level 11_0 (all the front accepts). */
    HRESULT (*CreateDevice)(void **device, void **context);
    /* The front object behind `object` was destroyed.  Once per back object. */
    void    (*Release)(RtD3dBackKind kind, void *object);
    /* Buffer `buffer` of the swap chain as a 2D texture: asked when the front makes its back
     * buffer, with 0 (the only buffer the front serves). */
    HRESULT (*SwapChainGetBuffer)(void *swapchain, UINT buffer, void **texture);

    /* ---- ID3D11Device: creators (the class linkage of a shader does not cross: the front stops on one) ---- */
    HRESULT (*CreateBuffer)(void *device, const D3D11_BUFFER_DESC *desc, const D3D11_SUBRESOURCE_DATA *initial, void **buffer);
    HRESULT (*CreateTexture1D)(void *device, const D3D11_TEXTURE1D_DESC *desc, const D3D11_SUBRESOURCE_DATA *initial, void **texture);
    HRESULT (*CreateTexture2D)(void *device, const D3D11_TEXTURE2D_DESC *desc, const D3D11_SUBRESOURCE_DATA *initial, void **texture);
    HRESULT (*CreateTexture3D)(void *device, const D3D11_TEXTURE3D_DESC *desc, const D3D11_SUBRESOURCE_DATA *initial, void **texture);
    HRESULT (*CreateShaderResourceView)(void *device, void *resource, const D3D11_SHADER_RESOURCE_VIEW_DESC *desc, void **view);
    HRESULT (*CreateUnorderedAccessView)(void *device, void *resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *desc, void **view);
    HRESULT (*CreateRenderTargetView)(void *device, void *resource, const D3D11_RENDER_TARGET_VIEW_DESC *desc, void **view);
    HRESULT (*CreateDepthStencilView)(void *device, void *resource, const D3D11_DEPTH_STENCIL_VIEW_DESC *desc, void **view);
    HRESULT (*CreateInputLayout)(void *device, const D3D11_INPUT_ELEMENT_DESC *elements, UINT count, const void *bytecode, SIZE_T length, void **layout);
    HRESULT (*CreateVertexShader)(void *device, const void *bytecode, SIZE_T length, void **shader);
    HRESULT (*CreateGeometryShader)(void *device, const void *bytecode, SIZE_T length, void **shader);
    HRESULT (*CreatePixelShader)(void *device, const void *bytecode, SIZE_T length, void **shader);
    HRESULT (*CreateComputeShader)(void *device, const void *bytecode, SIZE_T length, void **shader);
    HRESULT (*CreateBlendState)(void *device, const D3D11_BLEND_DESC *desc, void **state);
    HRESULT (*CreateDepthStencilState)(void *device, const D3D11_DEPTH_STENCIL_DESC *desc, void **state);
    HRESULT (*CreateRasterizerState)(void *device, const D3D11_RASTERIZER_DESC *desc, void **state);
    HRESULT (*CreateSamplerState)(void *device, const D3D11_SAMPLER_DESC *desc, void **state);
    HRESULT (*CreateQuery)(void *device, const D3D11_QUERY_DESC *desc, void **query);
    /* IDXGIDevice1 */
    HRESULT (*SetMaximumFrameLatency)(void *device, UINT latency);

    /* ---- the swap chain ---- */
    /* IDXGIFactory1::CreateSwapChain on the device's own factory.  The description is the game's:
     * OutputWindow is the runtime's window handle (rt/window.h), which the back end turns into
     * what it draws on. */
    HRESULT (*CreateSwapChain)(void *device, const DXGI_SWAP_CHAIN_DESC *desc, void **swapchain);
    /* The front has released the old back buffer's back object before it asks. */
    HRESULT (*ResizeBuffers)(void *swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
    HRESULT (*Present)(void *swapchain, UINT sync_interval, UINT flags);

    /* ---- ID3D11DeviceContext: binds of an array with a start slot ---- */
    void (*VSSetConstantBuffers)(void *context, UINT start, UINT count, void *const *buffers);
    void (*PSSetConstantBuffers)(void *context, UINT start, UINT count, void *const *buffers);
    void (*GSSetConstantBuffers)(void *context, UINT start, UINT count, void *const *buffers);
    void (*CSSetConstantBuffers)(void *context, UINT start, UINT count, void *const *buffers);
    void (*VSSetShaderResources)(void *context, UINT start, UINT count, void *const *views);
    void (*PSSetShaderResources)(void *context, UINT start, UINT count, void *const *views);
    void (*GSSetShaderResources)(void *context, UINT start, UINT count, void *const *views);
    void (*CSSetShaderResources)(void *context, UINT start, UINT count, void *const *views);
    void (*VSSetSamplers)(void *context, UINT start, UINT count, void *const *samplers);
    void (*PSSetSamplers)(void *context, UINT start, UINT count, void *const *samplers);
    void (*GSSetSamplers)(void *context, UINT start, UINT count, void *const *samplers);
    void (*CSSetSamplers)(void *context, UINT start, UINT count, void *const *samplers);
    void (*IASetVertexBuffers)(void *context, UINT start, UINT count, void *const *buffers, const UINT *strides, const UINT *offsets);
    void (*CSSetUnorderedAccessViews)(void *context, UINT start, UINT count, void *const *views, const UINT *initial_counts);

    /* ---- binds of one object (class instances do not cross: the front stops on any) ---- */
    void (*VSSetShader)(void *context, void *shader);
    void (*PSSetShader)(void *context, void *shader);
    void (*GSSetShader)(void *context, void *shader);
    void (*CSSetShader)(void *context, void *shader);
    void (*HSSetShader)(void *context, void *shader);   /* always NULL: the front has no hull or domain shader */
    void (*DSSetShader)(void *context, void *shader);
    void (*IASetInputLayout)(void *context, void *layout);
    void (*IASetIndexBuffer)(void *context, void *buffer, DXGI_FORMAT format, UINT offset);
    void (*RSSetState)(void *context, void *state);
    void (*OMSetBlendState)(void *context, void *state, const FLOAT *factor, UINT sample_mask);
    void (*OMSetDepthStencilState)(void *context, void *state, UINT stencil_ref);
    void (*OMSetRenderTargets)(void *context, UINT count, void *const *views, void *depth_stencil_view);
    void (*OMSetRenderTargetsAndUnorderedAccessViews)(void *context, UINT rtv_count, void *const *rtvs, void *depth_stencil_view,
                                                      UINT uav_start, UINT uav_count, void *const *uavs, const UINT *initial_counts);

    /* ---- state without an object ---- */
    void (*IASetPrimitiveTopology)(void *context, D3D11_PRIMITIVE_TOPOLOGY topology);
    void (*RSSetViewports)(void *context, UINT count, const D3D11_VIEWPORT *viewports);

    /* ---- draws and dispatches ---- */
    void (*Draw)(void *context, UINT vertex_count, UINT start_vertex);
    void (*DrawIndexed)(void *context, UINT index_count, UINT start_index, INT base_vertex);
    void (*DrawInstanced)(void *context, UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance);
    void (*DrawIndexedInstanced)(void *context, UINT index_count, UINT instance_count, UINT start_index, INT base_vertex, UINT start_instance);
    void (*DrawInstancedIndirect)(void *context, void *buffer, UINT offset);
    void (*Dispatch)(void *context, UINT x, UINT y, UINT z);
    void (*DispatchIndirect)(void *context, void *buffer, UINT offset);

    /* ---- clears ---- */
    void (*ClearRenderTargetView)(void *context, void *view, const FLOAT *rgba);
    void (*ClearDepthStencilView)(void *context, void *view, UINT flags, FLOAT depth, UINT8 stencil);
    void (*ClearUnorderedAccessViewFloat)(void *context, void *view, const FLOAT *values);

    /* ---- copies and updates ---- */
    void (*CopyResource)(void *context, void *dst, void *src);
    void (*CopySubresourceRegion)(void *context, void *dst, UINT dst_sub, UINT x, UINT y, UINT z, void *src, UINT src_sub, const D3D11_BOX *box);
    void (*ResolveSubresource)(void *context, void *dst, UINT dst_sub, void *src, UINT src_sub, DXGI_FORMAT format);
    void (*UpdateSubresource)(void *context, void *dst, UINT dst_sub, const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch);

    /* ---- mapping (rule 5) ---- */
    HRESULT (*Map)(void *context, void *resource, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped);
    void    (*Unmap)(void *context, void *resource, UINT sub);

    /* ---- queries (rule 6) ---- */
    void    (*Begin)(void *context, void *query);
    void    (*End)(void *context, void *query);
    HRESULT (*GetData)(void *context, void *query, void *data, UINT size, UINT flags);

    /* ---- the rest ---- */
    void (*ClearState)(void *context);
    void (*Flush)(void *context);
} RtD3dBack;

/* Every entry, in the table's order, with the interface of the front method it serves (its
 * method has the entry's name) or `-` for the seam's own.  RT_BACK_<Entry> is its number. */
#define RT_D3DBACK_ENTRIES(X) \
    X(CreateDevice, -) X(Release, -) X(SwapChainGetBuffer, -) \
    X(CreateBuffer, ID3D11Device) X(CreateTexture1D, ID3D11Device) X(CreateTexture2D, ID3D11Device) X(CreateTexture3D, ID3D11Device) \
    X(CreateShaderResourceView, ID3D11Device) X(CreateUnorderedAccessView, ID3D11Device) X(CreateRenderTargetView, ID3D11Device) \
    X(CreateDepthStencilView, ID3D11Device) X(CreateInputLayout, ID3D11Device) X(CreateVertexShader, ID3D11Device) \
    X(CreateGeometryShader, ID3D11Device) X(CreatePixelShader, ID3D11Device) X(CreateComputeShader, ID3D11Device) \
    X(CreateBlendState, ID3D11Device) X(CreateDepthStencilState, ID3D11Device) X(CreateRasterizerState, ID3D11Device) \
    X(CreateSamplerState, ID3D11Device) X(CreateQuery, ID3D11Device) X(SetMaximumFrameLatency, IDXGIDevice1) \
    X(CreateSwapChain, IDXGIFactory1) X(ResizeBuffers, IDXGISwapChain) X(Present, IDXGISwapChain) \
    X(VSSetConstantBuffers, ID3D11DeviceContext) X(PSSetConstantBuffers, ID3D11DeviceContext) X(GSSetConstantBuffers, ID3D11DeviceContext) \
    X(CSSetConstantBuffers, ID3D11DeviceContext) X(VSSetShaderResources, ID3D11DeviceContext) X(PSSetShaderResources, ID3D11DeviceContext) \
    X(GSSetShaderResources, ID3D11DeviceContext) X(CSSetShaderResources, ID3D11DeviceContext) X(VSSetSamplers, ID3D11DeviceContext) \
    X(PSSetSamplers, ID3D11DeviceContext) X(GSSetSamplers, ID3D11DeviceContext) X(CSSetSamplers, ID3D11DeviceContext) \
    X(IASetVertexBuffers, ID3D11DeviceContext) X(CSSetUnorderedAccessViews, ID3D11DeviceContext) \
    X(VSSetShader, ID3D11DeviceContext) X(PSSetShader, ID3D11DeviceContext) X(GSSetShader, ID3D11DeviceContext) \
    X(CSSetShader, ID3D11DeviceContext) X(HSSetShader, ID3D11DeviceContext) X(DSSetShader, ID3D11DeviceContext) \
    X(IASetInputLayout, ID3D11DeviceContext) X(IASetIndexBuffer, ID3D11DeviceContext) X(RSSetState, ID3D11DeviceContext) \
    X(OMSetBlendState, ID3D11DeviceContext) X(OMSetDepthStencilState, ID3D11DeviceContext) X(OMSetRenderTargets, ID3D11DeviceContext) \
    X(OMSetRenderTargetsAndUnorderedAccessViews, ID3D11DeviceContext) \
    X(IASetPrimitiveTopology, ID3D11DeviceContext) X(RSSetViewports, ID3D11DeviceContext) \
    X(Draw, ID3D11DeviceContext) X(DrawIndexed, ID3D11DeviceContext) X(DrawInstanced, ID3D11DeviceContext) \
    X(DrawIndexedInstanced, ID3D11DeviceContext) X(DrawInstancedIndirect, ID3D11DeviceContext) X(Dispatch, ID3D11DeviceContext) \
    X(DispatchIndirect, ID3D11DeviceContext) \
    X(ClearRenderTargetView, ID3D11DeviceContext) X(ClearDepthStencilView, ID3D11DeviceContext) X(ClearUnorderedAccessViewFloat, ID3D11DeviceContext) \
    X(CopyResource, ID3D11DeviceContext) X(CopySubresourceRegion, ID3D11DeviceContext) X(ResolveSubresource, ID3D11DeviceContext) \
    X(UpdateSubresource, ID3D11DeviceContext) X(Map, ID3D11DeviceContext) X(Unmap, ID3D11DeviceContext) \
    X(Begin, ID3D11DeviceContext) X(End, ID3D11DeviceContext) X(GetData, ID3D11DeviceContext) \
    X(ClearState, ID3D11DeviceContext) X(Flush, ID3D11DeviceContext)

#define RT_D3DBACK_ENUM_(entry, iface) RT_BACK_##entry,
typedef enum RtD3dBackEntry { RT_D3DBACK_ENTRIES(RT_D3DBACK_ENUM_) RT_BACK_N } RtD3dBackEntry;
#undef RT_D3DBACK_ENUM_

/* The table has one function pointer per entry, in that order (back.c checks every offset). */
_Static_assert(sizeof(RtD3dBack) == sizeof(const char *) + RT_BACK_N * sizeof(void (*)(void)), "RtD3dBack and RT_D3DBACK_ENTRIES list the same entries");

/* ---- choosing ----------------------------------------------------------------------------------------- */

/* The back end, or NULL for `none`.  Fixed from the first device on. */
extern _Atomic(const RtD3dBack *) rt_d3dback_active;
TF_INLINE const RtD3dBack *rt_d3dback(void) { return atomic_load_explicit(&rt_d3dback_active, memory_order_relaxed); }

/* Read RT_D3D_BACKEND, once (rt_nulldev_create calls it before it makes a device).  `dxmt`, and
 * any value that is not one of the three, is fatal with a report that says what is built. */
void rt_d3dback_select(void);
/* Choose without the environment, before the first device exists: NULL is `none`.  Fatal once
 * a device was made with another choice. */
void rt_d3dback_use(const RtD3dBack *back);
/* The recording back end (below). */
const RtD3dBack *rt_d3dback_record(void);

/* ---- for the front (runtime/nulldev/back.c) ------------------------------------------------------------- */

/* The back pointer of the front object `front` (any face): NULL for NULL.  Fatal when the
 * object's class has no back pointer (an adapter, an output), and when the object has none
 * although a back end is there (rule 2).  Only call it with a back end. */
void *rt_d3dback_of(void *front);
/* A creator is about to ask the back end for the object of `front` (not NULL): true when the
 * caller is the one to ask, and must then attach.  False when the object has a back object
 * already, or another thread is asking for it at this moment (a state object two threads were
 * handed at once): the front answers alone.  rt_d3dback_of waits for an attach that is under way. */
bool  rt_d3dback_claim(void *front);
/* Keep `back` as the back object of `front`.  Fatal when it has one, or `back` is NULL. */
void  rt_d3dback_attach(void *front, void *back);
/* The front object whose record is `state` (of class kind `kind`) is being destroyed: release
 * its back object, if any.  The classes' destroy functions call it. */
void  rt_d3dback_release(RtD3dBackKind kind, void **back);
/* Count entry `entry` as asked (the body is about to call it) or as answered by the front
 * alone, and remember which call of which method did (rule 1's line in a fatal report). */
void  rt_d3dback_reach(Ctx *ctx, RtD3dBackEntry entry);
void  rt_d3dback_alone_(Ctx *ctx, RtD3dBackEntry entry);
/* Rule 4: stop, naming the method, the back end and both results. */
_Noreturn void rt_d3dback_differs(Ctx *ctx, RtD3dBackEntry entry, HRESULT front, HRESULT back);

/* The seam's own counts: how often the front asked entry `entry`, how often it answered alone
 * where a body said so.  Both stay 0 with `none`. */
uint64_t rt_d3dback_asked(RtD3dBackEntry entry);
uint64_t rt_d3dback_alone(RtD3dBackEntry entry);
/* An entry's name ("DrawIndexed"), and the front method it serves as "Interface::Method", or
 * NULL for the seam's own three. */
const char *rt_d3dback_entry_name(RtD3dBackEntry entry);
const char *rt_d3dback_entry_method(RtD3dBackEntry entry);

/* Forward a call that returns nothing:   RT_BACK(ctx, DrawIndexed, c->child.back, n, start, base); */
#define RT_BACK(ctx, entry, ...) \
    do { const RtD3dBack *rt_b_ = rt_d3dback(); \
         if (__builtin_expect(rt_b_ != NULL, 0)) { rt_d3dback_reach((ctx), RT_BACK_##entry); rt_b_->entry(__VA_ARGS__); } } while (0)

/* Forward a call whose result the front already has; any other result stops (rule 4). */
#define RT_BACK_SAME(ctx, front_hr, entry, ...) \
    do { const RtD3dBack *rt_b_ = rt_d3dback(); \
         if (__builtin_expect(rt_b_ != NULL, 0)) { \
             rt_d3dback_reach((ctx), RT_BACK_##entry); \
             HRESULT rt_h_ = rt_b_->entry(__VA_ARGS__); \
             if (rt_h_ != (front_hr)) rt_d3dback_differs((ctx), RT_BACK_##entry, (front_hr), rt_h_); } } while (0)

/* A creator, after the front has its answer.  `made` is the front object it hands out, or NULL
 * when it made none (it only validated).  The arguments are the entry's WITHOUT the last, the
 * out-pointer.  An object that has a back object already (a state object that existed) and a
 * NULL are answered by the front alone; otherwise the back end is asked, anything but S_OK
 * with an object stops (rule 4), and the object is attached (rule 3). */
#define RT_BACK_CREATE(ctx, made, entry, ...) \
    do { const RtD3dBack *rt_b_ = rt_d3dback(); \
         if (__builtin_expect(rt_b_ != NULL, 0)) { \
             void *rt_m_ = (made), *rt_o_ = NULL; \
             if (!rt_m_ || !rt_d3dback_claim(rt_m_)) rt_d3dback_alone_((ctx), RT_BACK_##entry); \
             else { \
                 rt_d3dback_reach((ctx), RT_BACK_##entry); \
                 HRESULT rt_h_ = rt_b_->entry(__VA_ARGS__, &rt_o_); \
                 if (rt_h_ != S_OK || !rt_o_) rt_d3dback_differs((ctx), RT_BACK_##entry, S_OK, rt_o_ || rt_h_ != S_OK ? rt_h_ : E_POINTER); \
                 rt_d3dback_attach(rt_m_, rt_o_); } } } while (0)

/* The front answers this call alone, on purpose (a failure it returns, a path with nothing to
 * forward): write it just before that return, so that the counts add up. */
#define RT_BACK_ALONE(ctx, entry) \
    do { if (__builtin_expect(rt_d3dback() != NULL, 0)) rt_d3dback_alone_((ctx), RT_BACK_##entry); } while (0)

/* The back end's own answer (rule 6).  Only inside `if (rt_d3dback()) { ... }`; the body
 * checks the result and stops with rt_d3dback_differs on one it cannot hand to the game. */
#define RT_BACK_ANSWER(ctx, entry, ...) \
    (rt_d3dback_reach((ctx), RT_BACK_##entry), rt_d3dback()->entry(__VA_ARGS__))

/* ---- the recording back end (runtime/nulldev/back_record.c) ----------------------------------------------
 * Every entry counts itself and keeps its last arguments; an object argument that is not NULL
 * and not a live object of the recorder's own, of a kind the entry takes, is fatal (rule 2), and
 * so is a Release of anything else or a second Release.  Its objects are small blocks of host
 * memory.  HRESULT entries answer S_OK until rt_d3dback_record_answer says otherwise.  Map hands
 * out zeroed host memory of the subresource's size with the front's pitches (it keeps the
 * descriptions it was created with and uses the front's format table), valid until the object
 * is released; GetData writes the bytes of rt_d3dback_record_data, or zeroes. */

/* Times entry `entry` was reached. */
uint64_t rt_d3dback_record_count(RtD3dBackEntry entry);
/* Argument `i` of its last call, the first (device, context or swap chain) being 0, as 64 bits:
 * a pointer's address, an integer's value, a FLOAT's bits in the low 32.  An out-pointer reads
 * as the object the entry made; Map's argument 6 is the pointer it handed out. */
uint64_t rt_d3dback_record_arg(RtD3dBackEntry entry, unsigned i);
/* The elements of array argument `i` of its last call (copied when it was made): returns how
 * many it had and writes up to `max` of them to `out`; 0 for an array passed as NULL. */
unsigned rt_d3dback_record_array(RtD3dBackEntry entry, unsigned i, void **out, unsigned max);
/* What entry `entry` answers from now on (an HRESULT entry; S_OK to put it back). */
void     rt_d3dback_record_answer(RtD3dBackEntry entry, HRESULT hr);
/* What GetData writes from now on: `n` bytes (at most 16), or zeroes after n = 0. */
void     rt_d3dback_record_data(const void *bytes, size_t n);
/* Back objects of kind `kind` made so far, and alive now (made and not released). */
uint64_t rt_d3dback_record_made(RtD3dBackKind kind);
uint64_t rt_d3dback_record_live(RtD3dBackKind kind);
/* For a test that makes a front object without its creator (rt_nulldev_new): a new back object
 * of `kind`, as a creator's entry would have made it, to attach (rt_d3dback_attach).  A resource
 * made so has no description and cannot be mapped. */
void    *rt_d3dback_record_object(RtD3dBackKind kind);
/* The kind of a live object of the recorder's, or -1. */
int      rt_d3dback_record_kind(const void *object);

#ifdef __cplusplus
}
#endif

#endif /* RT_D3DBACK_H */
