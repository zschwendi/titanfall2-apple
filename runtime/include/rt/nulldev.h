/* The do-nothing Direct3D device (docs/handoff/design-null-device.md, candidate 1): our own
 * objects where Direct3D's would be, behind the interface layer (rt/com.h).
 *
 * This header is the OBJECT MODEL (unit d3d-contract; docs/handoff/d3d-contract.md is the
 * contract, with every method's answer and where it comes from).  The files:
 *     runtime/nulldev/objects.c                      the classes and what makes their objects
 *     runtime/nulldev/<Interface>/<Method>.c         one method body each (rt/com.h, COM_METHOD)
 *     runtime/tests/nulldev/<Interface>/<Method>.c   its test
 * Nothing of it survives Gate 4, where the same vtable slots forward to the native renderer;
 * the layer below (descriptions, vtables, counts) does.
 *
 * THE CLASSES, what each presents and what its record keeps.  "Presents" is the class's
 * faces: QueryInterface answers exactly those and their bases; any other IID is a stop,
 * except the documented refusals named under "refuses" (E_NOINTERFACE).
 *
 *   class            presents                          record     holds a reference to
 *   factory          IDXGIFactory1                     (none)     nothing
 *   adapter          IDXGIAdapter1                     NdAdapter  its factory
 *   output           IDXGIOutput                       NdOutput   its adapter
 *   device           ID3D11Device, IDXGIDevice1        NdDevice   its adapter
 *   context          ID3D11DeviceContext               NdContext  the device, everything bound
 *   swap chain       IDXGISwapChain                    NdSwapChain  the device, the factory, its back buffer
 *   buffer           ID3D11Buffer, IDXGIResource       NdBuffer   the device       refuses ID3D11Texture2D
 *   texture 1D       ID3D11Texture1D, IDXGIResource    NdTexture  the device       refuses ID3D11Texture2D
 *   texture 2D       ID3D11Texture2D, IDXGIResource,   NdTexture  the device       refuses IDXGISurface unless
 *                    IDXGISurface                                                  one mip level, one array slice
 *   texture 3D       ID3D11Texture3D, IDXGIResource    NdTexture  the device       refuses ID3D11Texture2D
 *   4 views          ID3D11ShaderResourceView, ...UnorderedAccessView, ...RenderTargetView,
 *                    ...DepthStencilView               NdView     the device, the resource
 *   4 states         ID3D11BlendState, ...DepthStencilState, ...RasterizerState, ...SamplerState
 *                                                      NdStateObject  the device
 *   4 shaders        ID3D11VertexShader, ...PixelShader, ...GeometryShader, ...ComputeShader
 *                                                      NdShader   the device
 *   input layout     ID3D11InputLayout                 NdInputLayout  the device
 *   query            ID3D11Query                       NdQuery    the device
 *   reflection       ID3D11ShaderReflection            NdReflection   nothing
 *   blob             ID3D10Blob                        NdBlob     nothing
 *
 * No class presents ID3D11HullShader or ID3D11DomainShader: the game never creates one
 * (finding 31, section 5), so no object of them can exist and HSGetShader / DSGetShader can
 * only ever hand back NULL.  A reflection object is what D3DReflect hands out
 * (runtime/shims/d3dcompiler_46/D3DReflect.c, runtime/nulldev/reflection.c): it belongs to no
 * device and keeps the input signature it parsed from the shader's bytes, not the bytes.
 * A blob is what D3DGetBlobPart hands out (runtime/shims/d3dcompiler_46/D3DGetBlobPart.c,
 * runtime/nulldev/blob.c): bytes of its own, here a container holding a shader's input signature.
 *
 * REFERENCES.  A record that keeps a pointer to another object keeps it alive, and its destroy
 * lets go.  The graph is a tree, so nothing keeps itself alive:  factory <- adapter <- { device,
 * output };  device <- { context, swap chain, every object a creator makes };  resource <- view;
 * back buffer <- swap chain;  bound object <- context.  Two kinds (rt/com.h, "references and
 * holds"; unit d3d-fix-2, F3 of docs/reviews/d3d-contract-check-2.md):
 *   a child on its parent   a REFERENCE (rt_com_addref): the game sees it, as on Windows, where a
 *                           device's count rises with every object it makes (Wine's creator
 *                           tests).  So a device with live children does not report 0 when the
 *                           game drops its own reference.
 *   everything else         a HOLD (rt_com_hold, through rt_nulldev_bind): a view on its resource,
 *                           the context on what is bound, the swap chain on its back buffer and
 *                           its factory.  The game does not see it: a texture with a view, bound
 *                           or not, still answers AddRef with 2, and the back buffer's Release
 *                           after one GetBuffer returns 0 while the swap chain keeps it alive.
 * rt_com_refs counts both kinds (what keeps the object alive); rt_com_public_refs the game's.
 *
 * MEMORY (unit d3d-resources; runtime/nulldev/memory.c; docs/handoff/d3d-resources.md).  A
 * buffer is one subresource of ByteWidth bytes; a texture has MipLevels * ArraySize of them
 * (a 3D texture MipLevels: all depth slices of a level are one subresource), each sized by
 * the format table below.  A subresource gets its bytes (an NdBlock) the first time the game
 * gives it initial data or maps it; the block ends at a guard page, so a write past the
 * subresource faults at the instruction that made it.  Nothing here ever computes resource
 * contents: a block holds what the game itself wrote (directly, or through a copy from
 * another block), each subresource records where its contents came from (ND_CONTENT_*), and a
 * read mapping of anything but the game's own bytes is a stop (ID3D11DeviceContext/Map.c has
 * the table).
 *
 * THE BACK END (unit pic-seam; rt/d3dback.h has the rules).  This device is the FRONT of a
 * seam: with a back end chosen (RT_D3D_BACKEND), every object of a class a renderer knows
 * (ND_BACK_KINDS: the device, the context, the swap chain and everything a creator makes) keeps
 * a pointer to the back end's own object, `back`, from its creation to its destruction, and a
 * body hands each call on after its checks.  With none (the default) `back` is NULL in every
 * record and nothing below changes.  The adapter, the output, the factory, the reflection and
 * the blob have no back object: the front answers for them alone.
 */
#ifndef RT_NULLDEV_H
#define RT_NULLDEV_H

#include "rt/com.h"
#include "rt/dxbc.h"
#include "rt/win/d3d11.h"
#include "rt/d3dback.h"          /* the seam: the back-end table and what a body forwards with */
#include "com_ids.h"            /* generated: rt_iface_<Interface>, and the Direct3D type names */

/* ---- identity and capabilities: what the native renderer reported on this Mac ------------------
 * Source of every value: docs/findings/35-dxmt-build.md, section 3 (tools/dxmt/device_probe run
 * against the DXMT build; its output is third_party/dxmt/build-native/tf2-probe/device_probe.out). */
#define ND_ADAPTER_DESCRIPTION   u"Apple M3 Pro"
#define ND_ADAPTER_VENDOR_ID     0x106bu
#define ND_ADAPTER_DEVICE_ID     0u
#define ND_ADAPTER_SUBSYS_ID     0u
#define ND_ADAPTER_REVISION      0u
#define ND_ADAPTER_VIDEO_MEMORY  UINT64_C(7151124480)   /* DedicatedVideoMemory; the two other sizes are 0 */
#define ND_ADAPTER_LUID_LOW      0x01000000u            /* AdapterLuid as the probe printed it: high d4070000, */
#define ND_ADAPTER_LUID_HIGH     ((LONG)0xd4070000u)    /* low 01000000 */
#define ND_ADAPTER_COUNT         1u                     /* "EnumAdapters1 count = 1" */
#define ND_OUTPUT_COUNT          1u                     /* one output: the desktop of rt/window.h (rt_desktop) */
#define ND_FEATURE_LEVEL         D3D_FEATURE_LEVEL_11_0 /* for a null feature-level list */
#define ND_MAX_SAMPLE_COUNT      4u                     /* quality levels for 1, 2, 4, 8, 16 samples: 1 1 1 0 0 */

/* The output's device name.  The native renderer reported "\\.\DISPLAY1" (finding 35,
 * docs/findings/35-dxmt-window.md, section 4); it is also Windows' name for the first
 * display device (from memory). */
#define ND_OUTPUT_DEVICE_NAME    u"\\\\.\\DISPLAY1"

/* What the native renderer on this Mac answered to ID3D11Device::CheckFormatSupport for every
 * format number from 1 to 132: the whole column of tools/dxmt/format_probe.out (one device
 * asked 132 times; finding 35, docs/findings/35-dxmt-build.md section 3 has the first 32, which
 * come out the same).  98 formats answer S_OK with their D3D11_FORMAT_SUPPORT bits; 34 answer
 * E_INVALIDARG and the device writes 0.
 *
 * rt_nulldev_format_measured: true with *hr and *support as measured for a number from 1 to
 * 132; false for 0 and for anything above 132, which nobody measured (the caller stops with
 * the format).  rt_nulldev_format_support: true and *support for the 98 the device has, false
 * for every other number. */
bool rt_nulldev_format_measured(DXGI_FORMAT format, HRESULT *hr, UINT *support);
bool rt_nulldev_format_support(DXGI_FORMAT format, UINT *support);

/* ---- limits of the context's recorded state (rt/win/d3d11.h has the numbers) -------------------- */
#define ND_CB_SLOTS        D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT            /* 14 */
#define ND_SRV_SLOTS       D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT                 /* 128 */
#define ND_SAMPLER_SLOTS   D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT                        /* 16 */
#define ND_RT_SLOTS        D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT                       /* 8 */
#define ND_VIEWPORTS       D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE     /* 16 */

/* ---- the records (rt_com_state(ctx, This, &class) returns one) ------------------------------------ */

typedef struct NdAdapter { IDXGIFactory1 *factory; } NdAdapter;
typedef struct NdOutput  { IDXGIAdapter1 *adapter; } NdOutput;

/* ONE THREAD IN THE CONTEXT AT A TIME (rt/d3dback.h, rule 8; runtime/nulldev/back.c).  A device
 * has one immediate context, which nothing locks; the guard is the device's, and every call of
 * ID3D11DeviceContext and the swap chain's Present and ResizeBuffers passes through it (the
 * generated thunks call rt_nulldev_guard_enter and _leave: runtime/com/guard.def). */
#define ND_GUARD_THREADS 8u
typedef struct NdGuard {
    _Atomic uint32_t inside;            /* the guest thread that is inside such a call, or 0 */
    uint32_t         depth;             /* how deep (a call made from inside one, on the same thread) */
    const void      *frame;             /* the shim frame of its outermost call: gone from the thread's chain when
                                           the call was left without returning (an unwind) */
    const RtComMethod *method;          /* that call's method, for the report */
    uint32_t         last;              /* the thread of the previous call */
    uint32_t         nthreads;          /* distinct threads seen, at most ND_GUARD_THREADS kept */
    struct { uint32_t id; uint64_t calls; } threads[ND_GUARD_THREADS];
    uint64_t         others;            /* calls of the threads beyond those */
    uint64_t        *last_calls;        /* the count of the thread of the previous call */
} NdGuard;

typedef struct NdDevice {
    IDXGIAdapter1    *adapter;
    void             *back;             /* the back end's device, or NULL (rt/d3dback.h) */
    NdGuard           guard;
    D3D_FEATURE_LEVEL level;            /* ND_FEATURE_LEVEL */
    UINT              max_frame_latency;/* IDXGIDevice1::SetMaximumFrameLatency; starts at 3, the documented default */
    void             *states[4];        /* the live state objects, one list per class (blend, depth-stencil,
                                           rasterizer, sampler), linked through NdStateObject.next; no reference
                                           is held: a state object takes itself out when it is destroyed */
    UINT              nstates[4];
} NdDevice;

/* Every object a device creates begins its record with this (the device's own reference). */
typedef struct NdChild {
    ID3D11Device *device;
    void         *back;                 /* the back end's object for this one, or NULL: with no back end, and until
                                           the creator has asked for it (rt/d3dback.h, rule 3) */
    uint32_t      kind;                 /* the object's class as an RtD3dBackKind (rt_nulldev_new sets it) */
} NdChild;

/* The immediate context.  It records only the bindings a Get method of finding 31's 146 reads
 * back; every other bind is accepted and forgotten.  Each non-NULL pointer here is a hold
 * (Microsoft, for each Set method of Direct3D 11: "The method will hold a reference to the
 * interfaces passed in"; that the application's count does not show it: rt/com.h).  The values after creation and after ClearState are the
 * defaults of ID3D11DeviceContext::ClearState's page: nothing bound, topology undefined (0),
 * blend factor {1,1,1,1}, sample mask 0xffffffff, stencil reference 0, no viewport. */
typedef struct NdContext {
    NdChild                    child;
    ID3D11Buffer              *vs_cb[ND_CB_SLOTS], *ps_cb[ND_CB_SLOTS];
    ID3D11ShaderResourceView  *ps_srv[ND_SRV_SLOTS];
    ID3D11SamplerState        *ps_sampler[ND_SAMPLER_SLOTS];
    ID3D11VertexShader        *vs;
    ID3D11PixelShader         *ps;
    ID3D11GeometryShader      *gs;
    ID3D11HullShader          *hs;          /* always NULL: no class presents the interface */
    ID3D11DomainShader        *ds;          /* always NULL */
    ID3D11InputLayout         *input_layout;
    D3D11_PRIMITIVE_TOPOLOGY   topology;
    ID3D11RenderTargetView    *rtv[ND_RT_SLOTS];
    ID3D11DepthStencilView    *dsv;
    ID3D11BlendState          *blend;
    FLOAT                      blend_factor[4];
    UINT                       sample_mask;
    ID3D11DepthStencilState   *depth_stencil;
    UINT                       stencil_ref;
    ID3D11RasterizerState     *rasterizer;
    UINT                       nviewports;
    D3D11_VIEWPORT             viewport[ND_VIEWPORTS];
} NdContext;

/* WHAT A SUBRESOURCE HOLDS (NdMemory.content, one per subresource; unit d3d-fix-2).  The
 * device computes no contents, so it can only say where the bytes came from, and a reader (a
 * read mapping, a copy) gets them when they are bytes the device has:
 *   NONE      nothing was ever put there (Windows: undefined contents; here zeroes)
 *   GAME      the game's own bytes, and the block holds them: initial data, a mapping that
 *             writes, UpdateSubresource, a copy from a subresource that held such bytes (a
 *             copy of memory the device holds is a memory copy, and is made)
 *   DROPPED   the game's own bytes, which the device did not keep, because the resource cannot
 *             be mapped (NdMemory.kept) or because they were copied from such a resource
 *   RENDERER  what only a renderer could have produced: a copy from a resource a renderer
 *             writes (NdMemory.output) or from a RENDERER subresource, a resolve
 *   PART      some units hold the game's bytes and the others nothing: NdBlock.written says
 *             which (unit d3d-fix-3; memory.c, "which bytes of a subresource are the game's").
 *             Only a resource that keeps bytes can be PART
 * GAME means every byte: a read mapping hands out a subresource only when it is GAME.  DROPPED
 * and RENDERER taint the whole subresource: a write of a part (a box) leaves them, the worse
 * of the two stays, and a write of the whole subresource replaces them.  NONE and GAME are
 * kept unit by unit: a box of the game's bytes written into NONE gives PART, and so does a box
 * copied from NONE into GAME.
 * WHAT THE GAME WRITES THROUGH THE POINTER OF A WRITING Map THE DEVICE CANNOT SEE, so it is
 * not counted as written: the bytes are kept (and copied by a copy), but a subresource that
 * was not GAME before the mapping is not GAME after it, and WRITE_DISCARD, after which Windows
 * calls the previous contents undefined, makes it NONE (ID3D11DeviceContext/Map.c). */
#define ND_CONTENT_NONE      0u
#define ND_CONTENT_GAME      1u
#define ND_CONTENT_DROPPED   2u
#define ND_CONTENT_RENDERER  3u
#define ND_CONTENT_PART      4u

/* The bytes of one subresource. */
typedef struct NdBlock {
    uint8_t *data;                      /* NULL until the game gives initial data or maps it; 16-byte aligned
                                           (D3D11_MAPPED_SUBRESOURCE's page: "the pointer is aligned to 16 bytes"
                                           from feature level 10_0); data + size rounded up to 16 is a guard page */
    void    *region;                    /* what was mapped from the host for it: the pages and the guard page */
    uint64_t region_bytes;
    uint32_t map_type;                  /* the D3D11_MAP it is mapped with now; 0 when it is not mapped */
    uint8_t *written;                   /* host memory, only while the subresource is ND_CONTENT_PART: one bit per
                                           unit (a byte of a buffer, an element, a 4 by 4 block), set where the
                                           unit holds the game's bytes */
    uint64_t written_units;             /* bits set in it */
} NdBlock;

/* The memory of a buffer or a texture. */
typedef struct NdMemory {
    uint64_t size;                      /* bytes of all subresources; 0: the resource has no memory here
                                           (a multisampled texture) and cannot be mapped */
    uint32_t nsub;                      /* subresources: 1 (buffer), MipLevels * ArraySize, MipLevels (3D) */
    uint32_t mapped;                    /* subresources mapped now (Map without Unmap) */
    uint8_t  kept;                      /* the device keeps this resource's bytes (blocks): only a resource the game
                                           can map (memory.c, "which resources keep bytes") */
    uint8_t  output;                    /* created with D3D11_BIND_RENDER_TARGET, _DEPTH_STENCIL or _UNORDERED_ACCESS:
                                           draws, clears and compute write it, none of which the device models, so
                                           every subresource of it counts as RENDERER whatever `content` says */
    uint8_t *back_mapped;               /* with a back end only (then `kept` is 0 and there are no blocks): nsub
                                           bytes, the D3D11_MAP a subresource is mapped with through the back end,
                                           or 0 (host memory; rt/d3dback.h, rule 5) */
    uint8_t *content;                   /* nsub of ND_CONTENT_* (host memory); NULL when size is 0 */
    NdBlock *blocks;                    /* nsub of them (host memory); NULL when size is 0 or the bytes are not kept */
} NdMemory;

typedef struct NdBuffer {
    NdChild           child;
    D3D11_BUFFER_DESC desc;             /* as the game passed it to CreateBuffer */
    NdMemory          mem;
} NdBuffer;

/* One record for the three texture classes; `dim` says which member of `desc` is meant.
 * The swap chain's back buffer is a texture 2D whose description the swap chain derives. */
typedef struct NdTexture {
    NdChild child;
    UINT    dim;                        /* D3D11_RESOURCE_DIMENSION_TEXTURE1D, _2D or _3D */
    union {
        D3D11_TEXTURE1D_DESC d1;
        D3D11_TEXTURE2D_DESC d2;
        D3D11_TEXTURE3D_DESC d3;
    } desc;                             /* as passed to the creator, MipLevels 0 replaced by the real count */
    NdMemory mem;
} NdTexture;

/* One record for the four view classes; the class says which member of `desc` is meant. */
typedef struct NdView {
    NdChild         child;
    ID3D11Resource *resource;           /* the face the creator was given; GetResource returns it */
    union {
        D3D11_SHADER_RESOURCE_VIEW_DESC  srv;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
        D3D11_RENDER_TARGET_VIEW_DESC    rtv;
        D3D11_DEPTH_STENCIL_VIEW_DESC    dsv;
    } desc;                             /* as Windows reads it back: the creator's, normalised, or the one a
                                           NULL description stands for (rt_nulldev_view_create) */
} NdView;

/* One record for the four state classes. */
typedef struct NdStateObject {
    NdChild child;
    union {
        D3D11_BLEND_DESC         blend;
        D3D11_DEPTH_STENCIL_DESC depth_stencil;
        D3D11_RASTERIZER_DESC    rasterizer;
        D3D11_SAMPLER_DESC       sampler;
    } desc;                             /* the creator's, normalised (rt_nulldev_state_create) */
    void *next;                         /* the next live state object of the same class and device, or NULL */
} NdStateObject;

typedef struct NdShader      { NdChild child; SIZE_T bytecode_length; } NdShader;
typedef struct NdInputLayout {
    NdChild child;
    UINT    nelements;
    D3D11_INPUT_ELEMENT_DESC *elements; /* a copy of the nelements descriptions (host memory); each SemanticName
                                           is still the game's pointer and is never followed */
} NdInputLayout;

typedef struct NdQuery {
    NdChild          child;
    D3D11_QUERY_DESC desc;
    uint32_t         begun;             /* 1 between Begin and End */
    uint32_t         ended;             /* End calls so far: GetData answers only when it is not 0 */
    uint64_t         end_ticks;         /* rt_nulldev_ticks() at the last End: a timestamp query's answer */
    uint64_t         draws_begin;       /* rt_nulldev_draws() at Begin */
    uint64_t         samples;           /* an occlusion query's answer, fixed at End: 0 unless RT_D3D_OCCLUSION=all */
} NdQuery;

typedef struct NdSwapChain {
    ID3D11Device        *device;        /* a reference (rt_nulldev_new) */
    void                *back;          /* the back end's swap chain, or NULL: where NdChild has it */
    uint32_t             kind;          /* RT_BK_SWAPCHAIN: the three fields are NdChild's, in its order */
    IDXGIFactory1       *factory;       /* a hold (rt_nulldev_bind) */
    ID3D11Texture2D     *back_buffer;   /* buffer 0, the only one served; a hold (rt_nulldev_swapchain_buffer) */
    DXGI_SWAP_CHAIN_DESC desc;          /* as passed to CreateSwapChain; ResizeBuffers updates it */
    BOOL                 fullscreen;    /* always FALSE: SetFullscreenState(TRUE) is a stop */
} NdSwapChain;

/* What D3DReflect parsed (runtime/nulldev/reflection.c says why this and not the bytes). */
typedef struct NdReflection {
    SIZE_T bytecode_length;             /* the size D3DReflect was given */
    UINT   ninputs;                     /* elements of the input signature */
    D3D11_SIGNATURE_PARAMETER_DESC *inputs; /* ninputs descriptions as GetInputParameterDesc copies them out, each
                                           SemanticName pointing at its name behind them: one block of host
                                           memory, valid until the object is destroyed */
} NdReflection;

/* A blob's bytes: host memory of its own, valid until the object is destroyed. */
typedef struct NdBlob { uint8_t *data; SIZE_T size; } NdBlob;

/* ---- the classes ------------------------------------------------------------------------------------ */

/* The classes whose objects have a back object, as a kind each (rt/d3dback.h, RtD3dBackKind):
 * RT_BK_KINDS of them.  rt_nulldev_back_kind: the kind of class `cls`, or -1. */
#define ND_BACK_KINDS RT_BK_KINDS
int rt_nulldev_back_kind(const RtComClass *cls);

extern const RtComClass rt_nulldev_factory_class;
extern const RtComClass rt_nulldev_adapter_class;
extern const RtComClass rt_nulldev_output_class;
extern const RtComClass rt_nulldev_device_class;        /* faces: ID3D11Device (identity), IDXGIDevice1 */
extern const RtComClass rt_nulldev_context_class;
extern const RtComClass rt_nulldev_swapchain_class;
extern const RtComClass rt_nulldev_buffer_class;
extern const RtComClass rt_nulldev_texture1d_class;
extern const RtComClass rt_nulldev_texture2d_class;     /* faces: ID3D11Texture2D (identity), IDXGIResource, IDXGISurface */
extern const RtComClass rt_nulldev_texture3d_class;
extern const RtComClass rt_nulldev_srv_class;
extern const RtComClass rt_nulldev_uav_class;
extern const RtComClass rt_nulldev_rtv_class;
extern const RtComClass rt_nulldev_dsv_class;
extern const RtComClass rt_nulldev_blend_class;
extern const RtComClass rt_nulldev_depthstencil_class;
extern const RtComClass rt_nulldev_rasterizer_class;
extern const RtComClass rt_nulldev_sampler_class;
extern const RtComClass rt_nulldev_vs_class;
extern const RtComClass rt_nulldev_ps_class;
extern const RtComClass rt_nulldev_gs_class;
extern const RtComClass rt_nulldev_cs_class;
extern const RtComClass rt_nulldev_inputlayout_class;
extern const RtComClass rt_nulldev_query_class;
extern const RtComClass rt_nulldev_reflection_class;
extern const RtComClass rt_nulldev_blob_class;

/* ---- making objects ------------------------------------------------------------------------------- */

/* A new device and its immediate context, each with one reference for the caller: what
 * D3D11CreateDevice hands out.  Behind them a new factory and a new adapter (the device's
 * parents; the device holds the adapter, the adapter the factory).  The context holds the
 * device, so the device's count is 2.  Any thread. */
void rt_nulldev_create(ID3D11Device **device, ID3D11DeviceContext **context);

/* A new adapter of `factory` (one reference for the caller; it holds one on the factory) and
 * a new output of `adapter` (the same).  Every adapter object is the one adapter of
 * ND_ADAPTER_*, every output object the one output: EnumAdapters1, EnumOutputs and
 * GetContainingOutput make a new object per call. */
IDXGIAdapter1 *rt_nulldev_adapter_new(IDXGIFactory1 *factory);
IDXGIOutput   *rt_nulldev_output_new(IDXGIAdapter1 *adapter);

/* A new object of one of the device-child classes (context, buffer, textures, views, states,
 * shaders, input layout, query) or of the swap chain class, with one reference for the
 * caller and its record zeroed except for the device, on which it holds a reference.  This
 * is what a creator starts from, and what a test uses to get an object to bind or to ask
 * before the creators exist: the caller fills the rest of the record.  Returns face 0.
 * Fatal for any other class. */
void *rt_nulldev_new(ID3D11Device *device, const RtComClass *cls);

/* ---- memory by format (runtime/nulldev/memory.c) ---------------------------------------------------- */

/* Bytes of one element of `format`, with *block 1, or of one 4 by 4 block, with *block 4 (the
 * BC formats).  The table and its sources are in memory.c.  False for DXGI_FORMAT_UNKNOWN and
 * for every format whose element is not a whole number of bytes or a 4 by 4 block (R1, the two
 * 4:2:2 packed formats, the video formats 100 to 114) and for a number that is no format: a
 * creator stops on those. */
bool rt_nulldev_format_bytes(DXGI_FORMAT format, uint32_t *bytes, uint32_t *block);
/* The family ("type group") of a format: the number of the typeless format that heads it, or
 * the format's own number when it stands alone; and whether a format is such a head (none of
 * its components has a type).  memory.c, "format families", has the table and its sources. */
unsigned rt_nulldev_format_family(DXGI_FORMAT format);
bool rt_nulldev_format_typeless(DXGI_FORMAT format);

/* One mip level of a texture: what D3D11_MAPPED_SUBRESOURCE and D3D11_SUBRESOURCE_DATA count in. */
typedef struct NdLevel {
    uint32_t width, height, depth;      /* of the level, in elements: each max(1, previous / 2) */
    uint32_t row_bytes;                 /* one row of elements, or of blocks: RowPitch.  No padding */
    uint32_t rows;                      /* rows of elements, or of blocks, in one depth slice */
    uint64_t slice_bytes;               /* row_bytes * rows: DepthPitch */
    uint64_t bytes;                     /* slice_bytes * depth: the whole subresource */
} NdLevel;

/* Mip levels of a full chain: down to 1 by 1 by 1. */
UINT rt_nulldev_full_mips(UINT width, UINT height, UINT depth);

/* Level `level` of a texture whose level 0 is width by height by depth (1 where the dimension
 * does not exist).  False when the format has no size here, and when a row would not fit 32
 * bits or the level 64 (dimensions no creator lets through: rt_nulldev_texture_new bounds them
 * by the feature level's limits first). */
bool rt_nulldev_level(DXGI_FORMAT format, UINT width, UINT height, UINT depth, UINT level, NdLevel *out);

/* A new buffer or texture (dim: D3D11_RESOURCE_DIMENSION_TEXTURE1D, _2D or _3D; desc: the
 * D3D11_TEXTURE1D_DESC, _2D_ or _3D_ of that dimension) with one reference for the caller, the
 * description kept, MipLevels 0 replaced by the full chain's count, the subresources counted
 * and sized.  `initial_data` only says whether the caller has initial data (the rule for
 * IMMUTABLE); rt_nulldev_fill copies it.  Stops, each printing the description: a size of 0;
 * a format with no size here; a usage, CPU access, bind or miscellaneous flag the device does
 * not know, or a combination Microsoft documents as invalid; more mip levels than the full
 * chain; an array size or a sample count of 0; a width, height, depth or array size above
 * feature level 11_0's limit (16384, or 2048 for a 3D texture's sides and for an array). */
ID3D11Buffer *rt_nulldev_buffer_new(Ctx *ctx, ID3D11Device *device, const D3D11_BUFFER_DESC *desc, bool initial_data);
void *rt_nulldev_texture_new(Ctx *ctx, ID3D11Device *device, UINT dim, const void *desc, bool initial_data);

/* The memory record of a buffer or texture (any face), and the layout of its subresource
 * `sub` (for a buffer: 1 row of ByteWidth bytes).  rt_nulldev_sub is false when `sub` is not
 * one of the resource's subresources. */
NdMemory *rt_nulldev_memory(void *resource);
bool rt_nulldev_sub(void *resource, UINT sub, NdLevel *out);

/* The block of subresource `sub`, its bytes allocated (zero-filled, ending at a guard page)
 * if they were not yet.  Fatal when the host has no memory, when `sub` is out of range or the
 * resource has no memory here. */
NdBlock *rt_nulldev_block(void *resource, UINT sub);

/* Copy initial data into every subresource: src[i] is subresource i (CreateTexture2D's page:
 * the array has MipLevels * ArraySize elements).  A buffer and a 1D texture take the bytes
 * as they lie; a 2D or 3D subresource is copied row by row, SysMemPitch bytes apart, slices
 * SysMemSlicePitch apart.  Every subresource is then GAME.  Stops: a NULL pSysMem; a pitch
 * smaller than a row, or a slice pitch smaller than a slice, where more than one has to be
 * read; a multisampled texture. */
void rt_nulldev_fill(Ctx *ctx, void *resource, const D3D11_SUBRESOURCE_DATA *src);

/* What subresource `sub` of a buffer or texture holds, for a reader: one of ND_CONTENT_*;
 * RENDERER for every subresource of an output resource.  Fatal when `sub` is out of range. */
unsigned rt_nulldev_content(void *resource, UINT sub);

/* Subresource `sub` was mapped with WRITE_DISCARD: its previous contents are undefined
 * (D3D11_MAP), and what the game writes now the device cannot see, so it holds nothing the
 * device can vouch for (NONE).  The bytes of the block stay. */
void rt_nulldev_discarded(void *resource, UINT sub);

/* How many units of subresource `sub` hold the game's bytes, and in *total how many it has
 * (a unit: a byte of a buffer, an element of a texture, a 4 by 4 block of a block format).
 * 0 for a subresource that is DROPPED or RENDERER. */
uint64_t rt_nulldev_known_units(void *resource, UINT sub, uint64_t *total);

/* ID3D11DeviceContext::CopySubresourceRegion and CopyResource: record what the destination
 * holds afterwards and, when source and destination both keep bytes and the source's are the
 * game's, copy them (memory.c, "copies", has the rules and their source).  `box` NULL is the
 * whole source subresource; an empty box does nothing.  Stops: a NULL resource; a subresource
 * neither has; a box outside the source, a destination too small, or (a block format) a box off
 * the block grid; source and destination formats whose elements differ in size; for
 * rt_nulldev_copy_all, resources of different kind, size or subresource count. */
void rt_nulldev_copy(Ctx *ctx, void *dst, UINT dst_sub, UINT x, UINT y, UINT z, void *src, UINT src_sub, const D3D11_BOX *box);
void rt_nulldev_copy_all(Ctx *ctx, void *dst, void *src);

/* ID3D11DeviceContext::UpdateSubresource: the game's bytes at `data` (rows `row_pitch` apart,
 * slices `depth_pitch` apart; a buffer's as they lie) go into `box` of the subresource, or
 * into all of it.  They are copied when the destination keeps bytes, and read once (so that an
 * unreadable pointer faults here, as it would in a device that uploads them) when it does not.
 * Stops: NULL data; a box outside the subresource or off the block grid; a pitch smaller than
 * a row where more than one row is read; an immutable or multisampled destination. */
void rt_nulldev_update(Ctx *ctx, void *dst, UINT dst_sub, const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch);

/* A renderer wrote subresource `sub` (ResolveSubresource's destination). */
void rt_nulldev_rendered(void *resource, UINT sub);

/* What a timestamp query counts in: the runtime's monotonic clock (rt/time.h), in units of
 * 100 ns, so ND_TICKS_PER_SECOND of them make a second. */
#define ND_TICKS_PER_SECOND UINT64_C(10000000)
uint64_t rt_nulldev_ticks(void);

/* ---- the ledger of invented answers, the switches, the totals (runtime/nulldev/ledger.c) --------
 * Unit d3d-fix-2 (docs/reviews/null-device-redteam.md, R20; the design's rule that a wrong
 * answer must be loud).  An answer the device cannot truly give is counted by kind, the first
 * of each kind is logged (`com` channel, warning) with its call site and value, and the
 * counts are printed with the call table. */
#define ND_INV_OCCLUSION  0u            /* GetData: an occlusion query's count of samples */
#define ND_INV_TIMESTAMP  1u            /* GetData: a timestamp */
#define ND_INV_DISJOINT   2u            /* GetData: the timestamps' frequency, and "not disjoint" */
#define ND_INV_EVENT      3u            /* GetData: an event query's "finished" */
#define ND_INV_READBACK   4u            /* Map: zeroes for contents only a renderer could have produced */
#define ND_INV_PRESENT    5u            /* Present: S_OK for a frame nobody saw */
#define ND_INV_PRESENT_0  6u            /* Present: a sync interval of 0 made to wait as an interval of 1 does */
#define ND_INV_KINDS      7u
void     rt_nulldev_invented(Ctx *ctx, unsigned kind, uint64_t value);
uint64_t rt_nulldev_invented_count(unsigned kind);

/* The run switches: environment variables, read at first use.
 *   RT_D3D_OCCLUSION   none (default): an occlusion query answers 0, the true count of a device
 *                      that draws nothing.  all: the first viewport's area times the sample count
 *                      of the first render target when a draw call came between Begin and End,
 *                      else 0 (the documented answer of a test nothing fails: the red team's R2).
 *   RT_D3D_READBACK    stop (default): a read mapping of contents only a renderer could have
 *                      produced is a stop.  zero: the block is filled with zeroes and handed
 *                      out, and the ledger counts it.
 *   RT_D3D_PRESENT     paced (default): Present with a sync interval of 1 to 4 returns at a
 *                      boundary of the 60 Hz mode the device reports, and so does a sync interval
 *                      of 0, as if it were 1: that is not what Windows does (0 presents at once);
 *                      the ledger counts each such call as an invented answer of its own kind
 *                      (ND_INV_PRESENT_0; IDXGISwapChain/Present.c says why).  immediate: at once,
 *                      whatever the interval.
 *   RT_D3D_KEEP        mappable (default): the device keeps bytes only for resources the game
 *                      can map.  all: for every buffer and texture, as before unit d3d-fix-2.
 * rt_nulldev_switches_reset: forget what was read (a test that changes the environment). */
bool rt_nulldev_keep_all(void);
bool rt_nulldev_occlusion_all(void);
bool rt_nulldev_readback_zero(void);
bool rt_nulldev_present_paced(void);
void rt_nulldev_switches_reset(void);

/* Wait until the n-th next boundary of 1/60 s (IDXGISwapChain::Present; never longer than a
 * quarter of a second: ledger.c). */
void rt_nulldev_pace(unsigned n);

/* Draw calls the context has taken so far, of all seven kinds (the layer's call counts). */
uint64_t rt_nulldev_draws(void);

/* The totals printed with the call table: buffers and textures alive, the blocks and bytes the
 * device holds for them, their peaks, and the bytes of the game's data it read and did not keep. */
typedef struct NdTotals { int64_t resources, resources_peak, blocks, bytes, bytes_peak, dropped_bytes, masks, masks_peak, masks_made; } NdTotals;
void rt_nulldev_account(int resource_delta, int block_delta, int64_t byte_delta, int64_t dropped);
/* A record of which units of a subresource the game wrote (NdBlock.written) was made (+1) or
 * freed (-1): the totals print how many were made, how many exist and the most at once. */
void rt_nulldev_account_mask(int delta);
void rt_nulldev_totals(NdTotals *t);

/* ---- the other creators' common parts (runtime/nulldev/objects.c) ----------------------------------- */

/* A state object of `cls` (blend, depth-stencil, rasterizer or sampler class) for `desc` (the
 * class's description structure).  Microsoft, CreateSamplerState: "If an application attempts
 * to create a sampler-state interface with the same state as an existing interface, the same
 * interface will be returned"; so a live object of the device with an equal description is
 * returned with one more reference, otherwise a new one.  Equal means equal after the
 * normalisation Windows applies (creators.c, state_normalise: fields a disabled part does not
 * use take fixed values), and the normalised description is the one kept.  S_OK.  Stops: a
 * NULL description or out-pointer; an enumerator outside its enumeration; the 4097th live
 * object of a class. */
HRESULT rt_nulldev_state_create(Ctx *ctx, ID3D11Device *device, const RtComClass *cls, const void *desc, void **out);

/* A view of `cls` (one of the four view classes) on `resource`, holding it.  `desc` is the
 * class's description or NULL.  The record keeps the description as Windows reads it back
 * (creators.c has the rules and their source, docs/reviews/d3d-contract-check-2.md, F1): in a
 * given one, format UNKNOWN on a texture is the texture's format and a count of 0xffffffff is
 * "all the rest"; a NULL one is derived from the texture: its format, every mip level (a
 * shader-resource view) or level 0 (the others), the dimension 2D, 2D array, cube or cube
 * array as the texture is.  S_OK; S_FALSE and no object when `out` is NULL (the creators'
 * pages: "Set this parameter to NULL to validate the other input parameters").  Stops: a NULL
 * resource, or one that is no buffer or texture of this layer; a view dimension that is not
 * one of the resource's kind; a NULL description for a buffer, a multisampled texture, a 1D
 * array, or a 3D texture unless the view is a shader-resource view. */
HRESULT rt_nulldev_view_create(Ctx *ctx, ID3D11Device *device, const RtComClass *cls, ID3D11Resource *resource,
                               const void *desc, void **out);

/* A shader of `cls` (one of the four shader classes) keeping only its byte count; nothing is
 * compiled.  S_OK; S_FALSE and no object when `out` is NULL (CreateVertexShader's page).
 * Stops: NULL bytecode, a length under 4, bytes that do not begin with "DXBC", a class linkage. */
HRESULT rt_nulldev_shader_create(Ctx *ctx, ID3D11Device *device, const RtComClass *cls, const void *bytecode,
                                 SIZE_T length, void *class_linkage, void **out);

/* A new reflection object (one reference for the caller) for the shader container bytes[0 .. size):
 * the container is opened (rt/dxbc.h: every check, the checksum among them) and its input
 * signature, the one ISGN or ISG1 chunk, is parsed into the record.  NULL, and `why`
 * (RT_DXBC_WHY bytes) says which check failed: one of rt/dxbc.h's, or a container with no
 * input-signature chunk or more than one.  The bytes are not kept.  Logs what it parsed
 * (channel com): chunk names and sizes and one line per input parameter. */
ID3D11ShaderReflection *rt_nulldev_reflection_new(const void *bytes, SIZE_T size, char *why);
void rt_nulldev_reflection_free(NdReflection *r);       /* for the class's destroy */

/* A new blob (one reference for the caller) holding the input-signature part of the shader
 * container bytes[0 .. size): a container of its own with that one chunk, as D3DGetBlobPart
 * makes it for D3D_BLOB_INPUT_SIGNATURE_BLOB (runtime/nulldev/blob.c has the layout and what
 * confirms it).  The source is opened first (rt/dxbc.h: every check).  NULL, and `why`
 * (RT_DXBC_WHY bytes) says why: a check of rt/dxbc.h, or a container that does not have exactly
 * one ISGN chunk.  The caller's bytes are not kept. */
ID3D10Blob *rt_nulldev_signature_blob_new(const void *bytes, SIZE_T size, char *why);
void rt_nulldev_blob_free(NdBlob *b);                   /* for the class's destroy */

/* Log (channel com, info) the elements of an input layout the game is creating and the input
 * signature in the bytes it passed with them, so that a run records what tools/trace/d3d/
 * reflect_compare.py compares with the recording.  It changes nothing and refuses nothing. */
void rt_nulldev_inputlayout_log(ID3D11InputLayout *layout, const D3D11_INPUT_ELEMENT_DESC *elements, UINT n,
                                const void *bytecode, SIZE_T length);

/* Keep a copy of `n` element descriptions in an input layout made with rt_nulldev_new. */
void rt_nulldev_inputlayout_set(ID3D11InputLayout *layout, const D3D11_INPUT_ELEMENT_DESC *elements, UINT n);

/* A new swap-chain back buffer for `sc` from its description (NdSwapChain.desc): a texture 2D
 * of BufferDesc's width, height and format and SampleDesc, one mip level, one slice, usage
 * DEFAULT, bind flags from BufferUsage.  The old one, if any, is released. */
void rt_nulldev_swapchain_buffer(Ctx *ctx, IDXGISwapChain *sc);

/* For the classes' destroy functions (objects.c): give back a resource's blocks, an input
 * layout's elements; take a state object out of its device's list. */
void rt_nulldev_memory_free(NdMemory *m);
void rt_nulldev_inputlayout_free(NdInputLayout *l);
void rt_nulldev_state_unlink(void *state, const RtComClass *cls);

/* ---- helpers for method bodies ------------------------------------------------------------------- */

/* `obj` as the guest passed it for a parameter that takes an object of class `cls`: returns
 * it when it is NULL or a face of a live object of that class; otherwise a stop that names
 * `what` (the parameter) and what the pointer is instead.  A body never stores or
 * dereferences a guest's object pointer that did not come through here or rt_com_state. */
void *rt_nulldev_arg(Ctx *ctx, void *obj, const RtComClass *cls, const char *what);

/* The same for a parameter that takes any resource (ID3D11Resource *): a buffer or a texture
 * of any dimension. */
void *rt_nulldev_resource_arg(Ctx *ctx, void *obj, const char *what);

/* IDXGIObject::GetParent for an object whose parent is `parent`: *out = the parent's face for
 * *riid with one more reference, and S_OK.  A stop when riid or out is NULL, and when *riid is
 * not the IID of a described interface the parent presents (Microsoft's page of GetParent
 * names no result for an interface the parent lacks: "Returns one of the DXGI_ERROR values";
 * read 2026-10-10). */
HRESULT rt_nulldev_get_parent(Ctx *ctx, void *parent, REFIID riid, void **out);

/* Bind: *slot = obj, with a hold on the new object (if not NULL) and the hold on the old one
 * ended (rt_com_hold, rt_com_unhold: the game's AddRef and Release do not see it; rt_com_refs
 * counts it).  `slot` is the address of a pointer field of a record. */
void rt_nulldev_bind(void *slot, void *obj);

/* Hand out: obj with one more reference for the receiver (the Get methods: "any returned
 * interfaces will have their reference count incremented by one"), or NULL for NULL. */
void *rt_nulldev_ref(void *obj);

/* ---- read and write: one subresource is not bound for both at once (runtime/nulldev/hazard.c) ----
 * Microsoft, PSSetShaderResources: "If an overlapping resource view is already bound to an
 * output slot, such as a rendertarget, then this API will fill the destination shader resource
 * slot with NULL"; OMSetRenderTargets: subresources "currently bound for reading ... those bind
 * points will be set to NULL".  Two views overlap when they share a subresource of one
 * resource (NdView.resource); hazard.c says which pairs it decides and where each rule was
 * read.  A pair of views of ONE resource it does not decide is a stop that names them. */

/* True when `view` (a view of any of the four classes; NULL gives false) overlaps a render
 * target recorded in c->rtv, or c->dsv unless that depth-stencil view is read-only for what
 * `view` reads.  PSSetShaderResources binds NULL in place of such a view. */
bool rt_nulldev_is_target(Ctx *ctx, const NdContext *c, void *view);

/* `target` (a render-target, depth-stencil or unordered-access view, or NULL: nothing) is being
 * bound for writing: every slot of c->ps_srv whose view overlaps it is bound to NULL, which
 * releases the slot's reference.  Call it for each view an output-merger Set binds. */
void rt_nulldev_unbind_readers(Ctx *ctx, NdContext *c, void *target);

/* The context's recorded state back to the defaults (NdContext), releasing what was bound. */
void rt_nulldev_context_reset(NdContext *c);

/* ---- the guard of the context (runtime/nulldev/back.c; NdGuard above) ---------------------------------
 * rt_nulldev_guard_enter: `This` (a context or a swap chain, as the thunk of method `m` got it)
 * is entered by the calling thread; returns what _leave takes, or NULL when there is nothing to
 * leave.  A second thread that enters while one is inside is a STOP that names both threads and
 * both methods; with RT_D3D_GUARD=count in the environment it is counted and logged instead (the
 * first of them), and the call goes on.  The distinct threads and the collisions are printed
 * with the call table ("d3d: the context was called from ...").  rt_nulldev_guard_stats: the
 * same numbers for a test: distinct threads so far, and in *collisions how many times a second
 * thread came in. */
void    *rt_nulldev_guard_enter(Ctx *ctx, void *This, const RtComMethod *m);
void     rt_nulldev_guard_leave(void *guard);
unsigned rt_nulldev_guard_stats(ID3D11Device *device, uint64_t *collisions);

#endif /* RT_NULLDEV_H */
