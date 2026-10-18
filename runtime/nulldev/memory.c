/* Memory by format for the do-nothing Direct3D device: the format table, mip levels and
 * pitches, the subresources of a buffer or texture and their bytes (rt/nulldev.h, "memory by
 * format"; docs/handoff/d3d-resources.md, section 2, has every statement with its source).
 *
 * SOURCES (Microsoft's pages, read 2026-10-10 by a subagent that returned the quoted text):
 *   DXGI_FORMAT enumeration: each format's description states its size ("A four-component,
 *     128-bit typeless format ..."); for 20 to 22 it gives "32-bit ... (with an additional 32
 *     bits)", 64.  The table below is that number divided by 8.
 *   "Texture Block Compression in Direct3D 11", column "Bytes per 4x4 pixel block": BC1 8,
 *     BC2 16, BC3 16, BC4 8, BC5 16, BC6H 16, BC7 16.
 *   The DDS programming guide: "For block-compressed formats, compute the pitch as:
 *     max( 1, ((width+3)/4) ) * block-size"; "For other formats, compute the pitch as:
 *     ( width * bits-per-pixel + 7 ) / 8".
 *   D3D11_SUBRESOURCE_DATA, Remarks: "the number of depth slices in each level is half the
 *     number of the previous level (minimum 1) and rounded down"; CreateTexture2D: "For a 32 x
 *     32 texture with a full mipmap chain, the pInitialData array has the following 6
 *     elements" (32, 16, 8, 4, 2, 1); CreateTexture3D: 32 x 32 x 4 has 6 levels, with 4, 2, 1,
 *     1, 1, 1 slices.
 *   D3D11CalcSubresource: "MipSlice + (ArraySlice * MipLevels)"; "For volume (3D) textures,
 *     all slices for a given mipmap level are a single subresource index."
 *   D3D11_MAPPED_SUBRESOURCE: pData "is aligned to 16 bytes" from feature level 10_0;
 *     RowPitch "the value that the runtime adds to pData to move from row to row", DepthPitch
 *     "from depth slice to depth slice"; "if a Buffer contains 8 bytes, the runtime assigns
 *     values to RowPitch and DepthPitch that are greater than or equal to 8".
 *   D3D11_CPU_ACCESS_FLAG: write access "must be created with either dynamic or staging
 *     usage", read access "must be created with staging usage"; CreateBuffer: pInitialData
 *     "cannot be NULL if the usage flag is D3D11_USAGE_IMMUTABLE"; D3D11_TEXTURE2D_DESC,
 *     MipLevels: "Use 1 for a multisampled texture; or 0 to generate a full set of subtextures."
 * The pitches here carry no padding (the page allows more: "might assign values ... larger
 * than anticipated"); this is the smallest block a game that honours the pitch can fill.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rt/nulldev.h"
#include "rt/time.h"

/* ---- the format table ------------------------------------------------------------------------------ */

static const struct { uint8_t first, last, bytes, block; } format_table[] = {
    {   1,   4, 16, 1 },    /* R32G32B32A32: 128 bits */
    {   5,   8, 12, 1 },    /* R32G32B32: 96 */
    {   9,  22,  8, 1 },    /* R16G16B16A16, R32G32, R32G8X24 and its three views: 64 */
    {  23,  47,  4, 1 },    /* R10G10B10A2, R11G11B10, R8G8B8A8, R16G16, R32, D32, R24G8 and its views: 32 */
    {  48,  59,  2, 1 },    /* R8G8, R16, D16: 16 */
    {  60,  65,  1, 1 },    /* R8, A8: 8 */
    {  67,  67,  4, 1 },    /* R9G9B9E5_SHAREDEXP: 32 */
    {  70,  72,  8, 4 },    /* BC1 */
    {  73,  78, 16, 4 },    /* BC2, BC3 */
    {  79,  81,  8, 4 },    /* BC4 */
    {  82,  84, 16, 4 },    /* BC5 */
    {  85,  86,  2, 1 },    /* B5G6R5, B5G5R5A1: 16 */
    {  87,  93,  4, 1 },    /* B8G8R8A8, B8G8R8X8, R10G10B10_XR_BIAS_A2 and their variants: 32 */
    {  94,  99, 16, 4 },    /* BC6H, BC7 */
    { 115, 115,  2, 1 },    /* B4G4R4A4: 16 */
};  /* not here: 0 UNKNOWN, 66 R1 (1 bit), 68 and 69 (4:2:2 pairs), 100 to 114 (video and palette formats) */

bool rt_nulldev_format_bytes(DXGI_FORMAT format, uint32_t *bytes, uint32_t *block) {
    for (unsigned i = 0; i < sizeof format_table / sizeof format_table[0]; i++)
        if ((uint32_t)format >= format_table[i].first && (uint32_t)format <= format_table[i].last) {
            *bytes = format_table[i].bytes;
            *block = format_table[i].block;
            return true;
        }
    return false;
}

/* FORMAT FAMILIES (unit d3d-fix-3).  A typeless format and the typed formats that give its bits
 * a meaning are one family (Microsoft's copy pages: "type group"): a typeless resource is viewed
 * through a typed format of its family, and a copy needs formats of one family.  The table is
 * the DXGI_FORMAT enumeration's own order and names (each *_TYPELESS is followed by its typed
 * formats); Microsoft's example, on D3D11_SHADER_RESOURCE_VIEW_DESC (read 2026-10-11): "a
 * DXGI_FORMAT_R32G32B32_TYPELESS resource can be viewed with one of these typed formats:
 * DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32_UINT, and DXGI_FORMAT_R32G32B32_SINT".
 * From memory, not read on a page: that B8G8R8A8_UNORM (87) and B8G8R8X8_UNORM (88) belong to
 * the families of 90 and 92, which follow them in the enumeration; that
 * R10G10B10_XR_BIAS_A2_UNORM (89) belongs to R10G10B10A2's; that A8_UNORM, R11G11B10_FLOAT,
 * R9G9B9E5 and the 16-bit B formats are each alone.  The recording agrees wherever it speaks
 * (docs/trace/d3d-creators.md: 65 view rows, every typeless texture viewed inside its family). */
static const struct { uint8_t first, last, family; } family_table[] = {
    { 1, 4, 1 }, { 5, 8, 5 }, { 9, 14, 9 }, { 15, 18, 15 }, { 19, 22, 19 }, { 23, 25, 23 }, { 27, 32, 27 }, { 33, 38, 33 },
    { 39, 43, 39 }, { 44, 47, 44 }, { 48, 52, 48 }, { 53, 59, 53 }, { 60, 64, 60 }, { 70, 72, 70 }, { 73, 75, 73 },
    { 76, 78, 76 }, { 79, 81, 79 }, { 82, 84, 82 }, { 87, 87, 90 }, { 88, 88, 92 }, { 89, 89, 23 }, { 90, 91, 90 },
    { 92, 93, 92 }, { 94, 96, 94 }, { 97, 99, 97 },
};

unsigned rt_nulldev_format_family(DXGI_FORMAT format) {
    for (unsigned i = 0; i < sizeof family_table / sizeof family_table[0]; i++)
        if ((uint32_t)format >= family_table[i].first && (uint32_t)format <= family_table[i].last) return family_table[i].family;
    return (unsigned)format;
}

/* A format none of whose components has a type: the head of a family.  (R24_UNORM_X8_TYPELESS,
 * X24_TYPELESS_G8_UINT and the two of the 64-bit depth family carry _TYPELESS in their names
 * and are view formats: the recording makes 7 shader-resource views with the first, S_OK.) */
bool rt_nulldev_format_typeless(DXGI_FORMAT format) {
    for (unsigned i = 0; i < sizeof family_table / sizeof family_table[0]; i++)
        if ((uint32_t)format == family_table[i].family && family_table[i].first == family_table[i].family) return true;
    return false;
}

static UINT half(UINT v) { return v > 1 ? v / 2 : 1; }

UINT rt_nulldev_full_mips(UINT width, UINT height, UINT depth) {
    UINT n = 1;
    for (; width > 1 || height > 1 || depth > 1; n++) { width = half(width); height = half(height); depth = half(depth); }
    return n;
}

bool rt_nulldev_level(DXGI_FORMAT format, UINT width, UINT height, UINT depth, UINT level, NdLevel *out) {
    uint32_t bytes, block;
    if (!rt_nulldev_format_bytes(format, &bytes, &block)) return false;
    for (UINT i = 0; i < level; i++) { width = half(width); height = half(height); depth = half(depth); }
    uint64_t cols = block == 4 ? (width + 3ull) / 4 : width, rows = block == 4 ? (height + 3ull) / 4 : height;
    if (!cols) cols = 1;
    if (!rows) rows = 1;
    if (cols * bytes > UINT32_MAX) return false;
    out->width = width; out->height = height; out->depth = depth;
    out->row_bytes = (uint32_t)(cols * bytes);
    out->rows = (uint32_t)rows;
    out->slice_bytes = (uint64_t)out->row_bytes * rows;     /* two 32-bit numbers: cannot overflow */
    return !__builtin_mul_overflow(out->slice_bytes, (uint64_t)depth, &out->bytes);
}

/* ---- buffers and textures ------------------------------------------------------------------------ */

static NdTexture *texture_of(void *resource) {
    const RtComClass *c = rt_com_class_of(resource);
    if (c == &rt_nulldev_texture1d_class || c == &rt_nulldev_texture2d_class || c == &rt_nulldev_texture3d_class)
        return rt_com_state_of(resource, c);
    return NULL;
}

NdMemory *rt_nulldev_memory(void *resource) {
    NdTexture *t = texture_of(resource);
    if (t) return &t->mem;
    return &((NdBuffer *)rt_com_state_of(resource, &rt_nulldev_buffer_class))->mem;    /* fatal for anything else */
}

/* What the three texture descriptions say, in one shape. */
typedef struct Shape { UINT width, height, depth, mips, array, format, samples, usage, bind, cpu, misc; } Shape;

static void shape_of(UINT dim, const void *desc, Shape *s) {
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D) {
        const D3D11_TEXTURE1D_DESC *d = desc;
        *s = (Shape){ d->Width, 1, 1, d->MipLevels, d->ArraySize, d->Format, 1, d->Usage, d->BindFlags, d->CPUAccessFlags, d->MiscFlags };
    } else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        const D3D11_TEXTURE2D_DESC *d = desc;
        *s = (Shape){ d->Width, d->Height, 1, d->MipLevels, d->ArraySize, d->Format, d->SampleDesc.Count, d->Usage, d->BindFlags,
                      d->CPUAccessFlags, d->MiscFlags };
    } else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
        const D3D11_TEXTURE3D_DESC *d = desc;
        *s = (Shape){ d->Width, d->Height, d->Depth, d->MipLevels, 1, d->Format, 1, d->Usage, d->BindFlags, d->CPUAccessFlags, d->MiscFlags };
    } else {
        rt_fatal("rt_nulldev: %u is not a texture dimension", (unsigned)dim);
    }
}

bool rt_nulldev_sub(void *resource, UINT sub, NdLevel *out) {
    NdTexture *t = texture_of(resource);
    if (!t) {
        NdBuffer *b = rt_com_state_of(resource, &rt_nulldev_buffer_class);
        if (sub) return false;
        *out = (NdLevel){ b->desc.ByteWidth, 1, 1, b->desc.ByteWidth, 1, b->desc.ByteWidth, b->desc.ByteWidth };
        return true;
    }
    Shape s;
    shape_of(t->dim, &t->desc, &s);
    if (sub >= t->mem.nsub) return false;
    return rt_nulldev_level(s.format, s.width, s.height, s.depth, sub % s.mips, out);
}

/* The flags this device knows: every D3D11_BIND_FLAG of Direct3D 11.0 (0x1 to 0x80), and of
 * D3D11_RESOURCE_MISC_FLAG those that need nothing from a device that draws nothing:
 * GENERATE_MIPS 0x1, TEXTURECUBE 0x4, DRAWINDIRECT_ARGS 0x10, BUFFER_ALLOW_RAW_VIEWS 0x20,
 * BUFFER_STRUCTURED 0x40, RESOURCE_CLAMP 0x80.  SHARED, SHARED_KEYEDMUTEX and GDI_COMPATIBLE
 * would need a handle or a GDI surface: a stop. */
#define KNOWN_BIND 0xffu
#define KNOWN_MISC 0xf5u

static const char *usage_problem(UINT usage, UINT bind, UINT cpu, UINT misc, bool initial_data) {
    if (usage > D3D11_USAGE_STAGING) return "a usage that is not one of D3D11_USAGE";
    if (cpu & ~(D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ)) return "a CPU access flag the device does not know";
    if (bind & ~KNOWN_BIND) return "a bind flag the device does not know";
    if (misc & ~KNOWN_MISC) return "a miscellaneous flag the device does not know";
    if ((cpu & D3D11_CPU_ACCESS_WRITE) && usage != D3D11_USAGE_DYNAMIC && usage != D3D11_USAGE_STAGING)
        return "CPU write access without dynamic or staging usage";
    if ((cpu & D3D11_CPU_ACCESS_READ) && usage != D3D11_USAGE_STAGING) return "CPU read access without staging usage";
    if (usage == D3D11_USAGE_IMMUTABLE && !initial_data) return "immutable usage without initial data";
    /* Unit d3d-fix-2, from the independent table (docs/reviews/d3d-format-table.md, point 4 and
     * C1, C2: Wine's test_resource_access, where creation fails with E_INVALIDARG; Microsoft,
     * D3D11_USAGE): a dynamic resource needs CPU write access, a staging resource read or write
     * access, and a staging resource has no bind flag. */
    if (usage == D3D11_USAGE_DYNAMIC && !(cpu & D3D11_CPU_ACCESS_WRITE)) return "dynamic usage without CPU write access";
    if (usage == D3D11_USAGE_STAGING && !cpu) return "staging usage without CPU access";
    if (usage == D3D11_USAGE_STAGING && bind) return "staging usage with a bind flag";
    return NULL;
}

/* WHICH RESOURCES KEEP BYTES (unit d3d-fix-2, item 5; docs/reviews/null-device-redteam.md, R7).
 * Only those the game can map: usage DYNAMIC or STAGING, or any CPU access flag (Microsoft,
 * D3D11_CPU_ACCESS_FLAG, memory.c's head: only such resources can be mapped; Map of any other is
 * a stop).  A DEFAULT or IMMUTABLE resource keeps its description, its size and one byte per
 * subresource that says what it holds (rt/nulldev.h, ND_CONTENT_*), and nothing else: its
 * initial data and updates are read once and dropped (DROPPED), so the game's texture set is
 * not held a second time in host memory, and no guard page is spent on it.  The recorded
 * session makes about 12,000 textures and 11,000 buffers; its only read-back is of a staging
 * texture (docs/trace/d3d-map.md).  RT_D3D_KEEP=all keeps every resource's bytes, as before
 * this unit: for measuring, and for a run that stops on a DROPPED read-back.
 * `content` and `blocks` are allocated here for a resource whose size and subresource count
 * are set. */
static void memory_init(NdMemory *m, UINT usage, UINT bind, UINT cpu) {
    m->kept = usage == D3D11_USAGE_DYNAMIC || usage == D3D11_USAGE_STAGING || cpu || rt_nulldev_keep_all();
    /* With a back end the front keeps bytes for no resource (rt/d3dback.h, rule 5): a mapping
     * hands out the back end's memory, and all the front records is which subresource is mapped. */
    if (rt_d3dback()) {
        m->kept = 0;
        m->back_mapped = calloc(m->nsub, 1);
        if (!m->back_mapped) rt_fatal("rt_nulldev: no host memory for the record of a resource's %u subresources", (unsigned)m->nsub);
    }
    m->output = (bind & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS)) != 0;
    m->content = calloc(m->nsub, 1);
    m->blocks = m->kept ? calloc(m->nsub, sizeof(NdBlock)) : NULL;
    if (!m->content || (m->kept && !m->blocks)) rt_fatal("rt_nulldev: no host memory for the record of a resource's %u subresources", (unsigned)m->nsub);
}

ID3D11Buffer *rt_nulldev_buffer_new(Ctx *ctx, ID3D11Device *device, const D3D11_BUFFER_DESC *d, bool initial_data) {
    const char *why = !d->ByteWidth ? "a size of 0" : usage_problem(d->Usage, d->BindFlags, d->CPUAccessFlags, d->MiscFlags, initial_data);
    if (why)
        RT_UNIMPLEMENTED(ctx, "a buffer description with %s: %u bytes, usage %u, bind flags 0x%x, CPU access flags 0x%x, "
                         "misc flags 0x%x, structure stride %u", why, (unsigned)d->ByteWidth, (unsigned)d->Usage,
                         (unsigned)d->BindFlags, (unsigned)d->CPUAccessFlags, (unsigned)d->MiscFlags, (unsigned)d->StructureByteStride);
    ID3D11Buffer *buffer = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    NdBuffer *b = rt_com_state_of(buffer, &rt_nulldev_buffer_class);
    b->desc = *d;
    b->mem.size = d->ByteWidth;
    b->mem.nsub = 1;
    memory_init(&b->mem, d->Usage, d->BindFlags, d->CPUAccessFlags);
    rt_nulldev_account(1, 0, 0, 0);
    return buffer;
}

void *rt_nulldev_texture_new(Ctx *ctx, ID3D11Device *device, UINT dim, const void *desc, bool initial_data) {
    Shape s;
    NdLevel l;
    shape_of(dim, desc, &s);
    UINT quality = dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D ? ((const D3D11_TEXTURE2D_DESC *)desc)->SampleDesc.Quality : 0;
    UINT full = rt_nulldev_full_mips(s.width, s.height, s.depth);
    /* The limits of feature level 11_0, which is the one this device reports (Microsoft's
     * resource limits and feature-level table, as docs/reviews/d3d-format-table.md, C14, reads
     * them: "1D width 16384, 1D array 2048 slices; 2D width and height 16384, 2D array 2048; 3D
     * each dimension 2048").  Windows refuses more; and inside them no product below can
     * overflow: a row is at most 16384 * 16 bytes, a slice 2^32, a 3D level 2^37, a resource of
     * 2048 slices with its mip chain under 2^44 (docs/reviews/d3d-code-review.md, G5). */
    UINT side = dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D ? D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION : D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    char over[80] = "";
    if (s.width > side) snprintf(over, sizeof over, "a width of %u, above the limit of %u", (unsigned)s.width, (unsigned)side);
    else if (s.height > side) snprintf(over, sizeof over, "a height of %u, above the limit of %u", (unsigned)s.height, (unsigned)side);
    else if (s.depth > side) snprintf(over, sizeof over, "a depth of %u, above the limit of %u", (unsigned)s.depth, (unsigned)side);
    else if (s.array > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION)
        snprintf(over, sizeof over, "an array size of %u, above the limit of %u", (unsigned)s.array, (unsigned)D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION);
    const char *why =
        over[0] ? over :
        !s.width || !s.height || !s.depth ? "a size of 0" :
        !rt_nulldev_level(s.format, s.width, s.height, s.depth, 0, &l) ? "a format that has no size in the format table" :
        s.mips > full ? "more mip levels than the full chain" :
        !s.array ? "an array size of 0" :
        !s.samples ? "a sample count of 0" :
        s.samples > 1 && s.mips != 1 ? "a multisampled texture that does not have exactly one mip level" :
        /* what the device's own CheckMultisampleQualityLevels answers: one quality level for 1, 2 and 4
           samples, none above (ND_MAX_SAMPLE_COUNT); Windows refuses a count or quality the device does
           not support (Wine's test_create_texture2d through the format-table review, C11; red team R9) */
        (s.samples & (s.samples - 1)) || s.samples > ND_MAX_SAMPLE_COUNT || quality ? "a sample count or quality the device's own CheckMultisampleQualityLevels refuses" :
        usage_problem(s.usage, s.bind, s.cpu, s.misc, initial_data);
    if (why)
        RT_UNIMPLEMENTED(ctx, "a texture %uD description with %s: width %u, height %u, depth %u, mip levels %u, array size %u, "
                         "format %u, samples %u, usage %u, bind flags 0x%x, CPU access flags 0x%x, misc flags 0x%x",
                         (unsigned)dim - 1, why, (unsigned)s.width, (unsigned)s.height, (unsigned)s.depth, (unsigned)s.mips,
                         (unsigned)s.array, (unsigned)s.format, (unsigned)s.samples, (unsigned)s.usage, (unsigned)s.bind,
                         (unsigned)s.cpu, (unsigned)s.misc);
    if (!s.mips) s.mips = full;
    const RtComClass *cls = dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D ? &rt_nulldev_texture1d_class :
                            dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D ? &rt_nulldev_texture2d_class : &rt_nulldev_texture3d_class;
    void *texture = rt_nulldev_new(device, cls);
    NdTexture *t = rt_com_state_of(texture, cls);
    t->dim = dim;
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D) { t->desc.d1 = *(const D3D11_TEXTURE1D_DESC *)desc; t->desc.d1.MipLevels = s.mips; }
    else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { t->desc.d2 = *(const D3D11_TEXTURE2D_DESC *)desc; t->desc.d2.MipLevels = s.mips; }
    else { t->desc.d3 = *(const D3D11_TEXTURE3D_DESC *)desc; t->desc.d3.MipLevels = s.mips; }
    t->mem.nsub = s.mips * s.array;
    if (s.samples == 1) {               /* a multisampled texture has no memory here: nothing documents its layout */
        for (UINT m = 0; m < s.mips; m++) {
            rt_nulldev_level(s.format, s.width, s.height, s.depth, m, &l);
            t->mem.size += l.bytes * s.array;
        }
        memory_init(&t->mem, s.usage, s.bind, s.cpu);
    }
    rt_nulldev_account(1, 0, 0, 0);
    return texture;
}

NdBlock *rt_nulldev_block(void *resource, UINT sub) {
    NdMemory *m = rt_nulldev_memory(resource);
    NdLevel l;
    if (!m->blocks || !rt_nulldev_sub(resource, sub, &l))
        rt_fatal("rt_nulldev_block: subresource %u of a resource with %u, %llu bytes", (unsigned)sub, (unsigned)m->nsub,
                 (unsigned long long)m->size);
    NdBlock *b = &m->blocks[sub];
    if (!b->data) {
        uint64_t page = (uint64_t)getpagesize(), body = (l.bytes + 15) & ~UINT64_C(15);
        uint64_t len = ((body + page - 1) & ~(page - 1)) + page;
        uint8_t *r = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (r == MAP_FAILED || mprotect(r + len - page, page, PROT_NONE))
            rt_fatal("rt_nulldev_block: the host gave no memory for a subresource of %llu bytes", (unsigned long long)l.bytes);
        b->region = r;
        b->region_bytes = len;
        b->data = r + len - page - body;        /* the last 16-byte unit ends where the guard page begins */
        rt_nulldev_account(0, 1, (int64_t)(len - page), 0);
    }
    return b;
}

void rt_nulldev_memory_free(NdMemory *m) {
    for (UINT i = 0; m->blocks && i < m->nsub; i++) {
        if (m->blocks[i].region) {
            munmap(m->blocks[i].region, m->blocks[i].region_bytes);
            rt_nulldev_account(0, -1, -(int64_t)(m->blocks[i].region_bytes - (uint64_t)getpagesize()), 0);
        }
        if (m->blocks[i].written) { free(m->blocks[i].written); rt_nulldev_account_mask(-1); }
    }
    rt_nulldev_account(-1, 0, 0, 0);
    free(m->blocks);
    free(m->content);
    free(m->back_mapped);
    m->back_mapped = NULL;
    m->blocks = NULL;
    m->content = NULL;
}

/* ---- what a subresource holds, and what writes it (rt/nulldev.h, ND_CONTENT_*) ------------------ */

typedef struct Geo {                    /* one subresource, in units: bytes (buffer), elements, or 4 by 4 blocks */
    NdLevel  l;
    uint32_t unit, block;               /* bytes of a unit; elements a unit covers a side: 1 or 4 */
    uint32_t cols;                      /* units in a row */
    bool     buffer;
} Geo;

typedef struct Span { uint32_t x, y, z, w, h, d; } Span;


unsigned rt_nulldev_content(void *resource, UINT sub) {
    NdMemory *m = rt_nulldev_memory(resource);
    if (!m->content || sub >= m->nsub)
        rt_fatal("rt_nulldev_content: subresource %u of a resource with %u, %llu bytes", (unsigned)sub, (unsigned)m->nsub,
                 (unsigned long long)m->size);
    return m->output ? ND_CONTENT_RENDERER : m->content[sub];
}

/* WHICH BYTES OF A SUBRESOURCE ARE THE GAME'S (unit d3d-fix-3; docs/reviews/
 * night-2026-10-11-code-review.md, finding 3).  A subresource whose content is GAME holds the
 * game's bytes in every unit, one that is NONE in none.  Between the two is PART: a bitmap
 * (NdBlock.written) with one bit per UNIT of the subresource (a byte of a buffer, an element of
 * a texture, a 4 by 4 block of a block-compressed one: the grain of a box), set where a call
 * the device sees put the game's bytes: UpdateSubresource, a copy from units that were the
 * game's.  The bitmap exists only while the subresource is PART: it is made at the first box
 * that leaves a subresource neither all nor nothing and freed when the last unit is written or
 * unwritten, so it costs units / 8 bytes of host memory for that time (8 bytes for a 64-byte
 * staging buffer, 32 KiB for a 512 by 512 texture), and only resources that keep bytes (those
 * the game can map) can have one: of the others every written byte is DROPPED, which no reader
 * gets.  The device's totals count them (rt_nulldev_account_mask). */

static void mask_drop(NdMemory *m, UINT sub) {
    if (m->blocks && m->blocks[sub].written) {
        free(m->blocks[sub].written);
        m->blocks[sub].written = NULL;
        m->blocks[sub].written_units = 0;
        rt_nulldev_account_mask(-1);
    }
}

/* All of subresource `sub` receives contents of kind `kind`. */
static void content_all(NdMemory *m, UINT sub, unsigned kind) {
    mask_drop(m, sub);
    m->content[sub] = (uint8_t)kind;
}

/* Units first to first + n of the bitmap become set or clear; the count of set units follows. */
static void mask_run(NdBlock *b, uint64_t first, uint64_t n, bool on) {
    uint64_t i = first, end = first + n;
    while (i < end) {
        if (!(i & 7) && end - i >= 8) {                 /* a whole byte of the bitmap */
            uint8_t *p = &b->written[i >> 3];
            unsigned had = (unsigned)__builtin_popcount(*p);
            if (on) { b->written_units += 8 - had; *p = 0xff; } else { b->written_units -= had; *p = 0; }
            i += 8;
            continue;
        }
        uint8_t bit = (uint8_t)(1u << (i & 7)), *p = &b->written[i >> 3];
        if (on && !(*p & bit)) { *p |= bit; b->written_units++; }
        else if (!on && (*p & bit)) { *p &= (uint8_t)~bit; b->written_units--; }
        i++;
    }
}

static bool mask_bit(const NdBlock *b, uint64_t i) { return (b->written[i >> 3] >> (i & 7)) & 1; }

static bool whole_of(const Geo *g, const Span *s);

static uint64_t units_of(const Geo *g) { return (uint64_t)g->cols * g->l.rows * g->l.depth; }

/* Span `s` of subresource `sub` (its shape: `g`) receives contents of kind `kind`: NONE (what
 * Windows calls undefined), GAME (the game's bytes, which the block now holds), DROPPED or
 * RENDERER.  The two last are a taint of the whole subresource, as before this unit: over a
 * part they stay whatever is written there afterwards, the worse of two stays, and only a write
 * of the whole subresource removes them.  NONE and GAME are kept unit by unit. */
static void content_put(void *resource, NdMemory *m, UINT sub, const Geo *g, const Span *s, unsigned kind) {
    bool whole = whole_of(g, s);
    uint8_t *c = &m->content[sub];
    bool tainted = *c == ND_CONTENT_DROPPED || *c == ND_CONTENT_RENDERER;
    if (kind == ND_CONTENT_DROPPED || kind == ND_CONTENT_RENDERER) {
        if (whole || !tainted || kind > *c) content_all(m, sub, kind);
        return;
    }
    if (whole) { content_all(m, sub, kind); return; }
    if (tainted || *c == kind) return;
    if (!m->kept) return;               /* no bytes, so no GAME: a part that is NONE over NONE */
    NdBlock *b = rt_nulldev_block(resource, sub);
    uint64_t total = units_of(g);
    if (!b->written) {
        b->written = malloc((size_t)((total + 7) / 8));
        if (!b->written) rt_fatal("rt_nulldev: no host memory for the record of which of %llu units of a subresource the game wrote", (unsigned long long)total);
        memset(b->written, *c == ND_CONTENT_GAME ? 0xff : 0, (size_t)((total + 7) / 8));
        b->written_units = *c == ND_CONTENT_GAME ? total : 0;
        *c = ND_CONTENT_PART;
        rt_nulldev_account_mask(1);
    }
    for (uint32_t k = 0; k < s->d; k++)
        for (uint32_t j = 0; j < s->h; j++)
            mask_run(b, ((uint64_t)(s->z + k) * g->l.rows + s->y + j) * g->cols + s->x, s->w, kind == ND_CONTENT_GAME);
    if (b->written_units == total) content_all(m, sub, ND_CONTENT_GAME);
    else if (!b->written_units) content_all(m, sub, ND_CONTENT_NONE);
}

void rt_nulldev_discarded(void *resource, UINT sub) {
    NdMemory *m = rt_nulldev_memory(resource);
    if (!m->content || sub >= m->nsub)
        rt_fatal("rt_nulldev_discarded: subresource %u of a resource with %u", (unsigned)sub, (unsigned)m->nsub);
    content_all(m, sub, ND_CONTENT_NONE);
}

void rt_nulldev_rendered(void *resource, UINT sub) {
    NdMemory *m = rt_nulldev_memory(resource);
    if (!m->content || sub >= m->nsub)
        rt_fatal("rt_nulldev_rendered: subresource %u of a resource with %u", (unsigned)sub, (unsigned)m->nsub);
    content_all(m, sub, ND_CONTENT_RENDERER);
}

uint64_t rt_nulldev_known_units(void *resource, UINT sub, uint64_t *total) {
    NdMemory *m = rt_nulldev_memory(resource);
    NdLevel l;
    uint32_t unit = 1, block = 1;
    NdTexture *t = texture_of(resource);
    if (!m->content || !rt_nulldev_sub(resource, sub, &l))
        rt_fatal("rt_nulldev_known_units: subresource %u of a resource with %u", (unsigned)sub, (unsigned)m->nsub);
    if (t) {
        Shape s;
        shape_of(t->dim, &t->desc, &s);
        rt_nulldev_format_bytes(s.format, &unit, &block);
    }
    *total = (uint64_t)(l.row_bytes / unit) * l.rows * l.depth;
    return m->content[sub] == ND_CONTENT_GAME ? *total :
           m->content[sub] == ND_CONTENT_PART ? m->blocks[sub].written_units : 0;
}

/* Read every page of n bytes at p, so that an unreadable pointer faults here (in the method
 * the game called) as it would in a device that uploads the bytes. */
static void touch(const uint8_t *p, uint64_t n) {
    volatile uint8_t sink = 0;
    for (uint64_t i = 0; i < n; i += 4096) sink ^= p[i];
    if (n) sink ^= p[n - 1];
    (void)sink;
}

void rt_nulldev_fill(Ctx *ctx, void *resource, const D3D11_SUBRESOURCE_DATA *src) {
    NdMemory *m = rt_nulldev_memory(resource);
    NdTexture *t = texture_of(resource);
    bool rows_apart = t && t->dim != D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    if (!m->size) RT_UNIMPLEMENTED(ctx, "initial data for a multisampled texture");
    for (UINT sub = 0; sub < m->nsub; sub++) {
        NdLevel l;
        rt_nulldev_sub(resource, sub, &l);
        const uint8_t *p = src[sub].pSysMem;
        uint64_t pitch = src[sub].SysMemPitch, slice = src[sub].SysMemSlicePitch;
        if (!p) RT_UNIMPLEMENTED(ctx, "initial data whose pSysMem is NULL (subresource %u)", (unsigned)sub);
        if (rows_apart && l.rows > 1 && pitch < l.row_bytes)
            RT_UNIMPLEMENTED(ctx, "initial data with SysMemPitch %u, smaller than a row of %u bytes (subresource %u, %u rows)",
                             (unsigned)pitch, (unsigned)l.row_bytes, (unsigned)sub, (unsigned)l.rows);
        if (rows_apart && l.depth > 1 && slice < pitch * (l.rows - 1) + l.row_bytes)
            RT_UNIMPLEMENTED(ctx, "initial data with SysMemSlicePitch %u, smaller than a slice of %u rows %u bytes apart "
                             "(subresource %u)", (unsigned)slice, (unsigned)l.rows, (unsigned)pitch, (unsigned)sub);
        uint8_t *to = m->kept ? rt_nulldev_block(resource, sub)->data : NULL;
        if (!rows_apart) {
            if (to) memcpy(to, p, l.bytes); else touch(p, l.bytes);
            if (!to) rt_nulldev_account(0, 0, 0, (int64_t)l.bytes);
        } else {
            for (UINT z = 0; z < l.depth; z++)
                for (UINT y = 0; y < l.rows; y++) {
                    const uint8_t *row = p + z * slice + y * pitch;
                    if (to) memcpy(to + z * l.slice_bytes + (uint64_t)y * l.row_bytes, row, l.row_bytes); else touch(row, l.row_bytes);
                }
            if (!to) rt_nulldev_account(0, 0, 0, (int64_t)l.bytes);
        }
        content_all(m, sub, m->kept ? ND_CONTENT_GAME : ND_CONTENT_DROPPED);
    }
}

/* ---- copies and updates --------------------------------------------------------------------------
 * Microsoft, read 2026-10-10 by a subagent that returned the quoted text:
 *   CopySubresourceRegion: pSrcBox "If NULL, the entire source subresource is copied.  The box
 *     must fit within the source resource."; "An empty box results in a no-op.  A box is empty
 *     if the top value is greater than or equal to the bottom value, or the left value is
 *     greater than or equal to the right value, or the front value is greater than or equal to
 *     the back value."; "If the resources are buffers, all coordinates are in bytes; if the
 *     resources are textures, all coordinates are in texels."; "If you try and copy outside the
 *     destination resource or specify a source box that is larger than the source resource, the
 *     behavior of CopySubresourceRegion is undefined."; source and destination "Must be
 *     different subresources (although they can be from the same resource)", "Must be the same
 *     type", "Must have compatible DXGI formats (identical or from the same type group)".
 *   CopyResource: the two "Must be different resources", "Must be the same type", "Must have
 *     identical dimensions (including width, height, depth, and size as appropriate)"; "You
 *     can't use an Immutable resource as a destination."
 *   UpdateSubresource: pDstBox "Coordinates are in bytes for buffers and in texels for
 *     textures.  If NULL, the data is written to the destination subresource with no offset.";
 *     "An empty box results in a no-op."; SrcRowPitch "The size of one row of the source data.",
 *     SrcDepthPitch "The size of one depth slice of source data."; "A resource cannot be used
 *     as a destination if: the resource is created with immutable or dynamic usage. the resource
 *     is created as a depth-stencil resource. the resource is created with multisampling
 *     capability".
 * What is "undefined" or "cannot be used" there is a stop here: a real device does something
 * with it that nothing documents.  Both pages also say the resources may not be mapped: a
 * stop since unit d3d-fix-3 (rt_nulldev_copy); the 28 dynamic buffers the game keeps mapped
 * for the whole session (docs/trace/d3d-map.md) are in no recorded copy (the replay).  From memory: a row of a block-compressed source is a row of 4 by 4 blocks
 * (as for initial data); the box of a block format lies on the block grid; an immutable
 * destination of CopySubresourceRegion (its page does not say; CopyResource's does).
 *
 * WHAT A COPY DOES HERE.  It is a memory copy exactly when the source's bytes are the game's
 * and the device has them (GAME) and the destination keeps bytes: then the bytes are copied
 * and the destination is GAME.  Otherwise no byte moves and the destination records what it
 * would hold: RENDERER from an output resource or a RENDERER subresource, DROPPED from the
 * game's bytes the device did not keep or cannot keep, NONE (and zeroes, where bytes are kept)
 * from a subresource nothing was ever put in.  ID3D11DeviceContext/Map.c reads the record. */

static void *copy_arg(Ctx *ctx, void *obj, const char *what) {
    if (!obj) RT_UNIMPLEMENTED(ctx, "a copy or an update with a NULL %s", what);
    return rt_nulldev_resource_arg(ctx, obj, what);
}

static void geo_of(Ctx *ctx, void *resource, UINT sub, const char *what, Geo *g) {
    NdMemory *m = rt_nulldev_memory(resource);
    NdTexture *t = texture_of(resource);
    if (!m->size) RT_UNIMPLEMENTED(ctx, "a copy or an update whose %s is a multisampled texture: the device keeps no layout for one", what);
    if (!rt_nulldev_sub(resource, sub, &g->l))
        RT_UNIMPLEMENTED(ctx, "a copy or an update of a subresource the %s does not have: %u of %u", what, (unsigned)sub, (unsigned)m->nsub);
    g->buffer = !t;
    g->unit = 1; g->block = 1;
    if (t) {
        Shape s;
        shape_of(t->dim, &t->desc, &s);
        rt_nulldev_format_bytes(s.format, &g->unit, &g->block);
    }
    g->cols = g->l.row_bytes / g->unit;
}

/* `box` (NULL: everything) of subresource g, in units.  False for an empty box. */
static bool span_of(Ctx *ctx, const Geo *g, const D3D11_BOX *box, const char *what, Span *s) {
    if (!box) { *s = (Span){ 0, 0, 0, g->cols, g->l.rows, g->l.depth }; return true; }
    if (box->left >= box->right || box->top >= box->bottom || box->front >= box->back) return false;
    if (box->right > g->l.width || box->bottom > g->l.height || box->back > g->l.depth)
        RT_UNIMPLEMENTED(ctx, "a box (%u to %u, %u to %u, %u to %u) outside the %s, a subresource of %u by %u by %u: Microsoft "
                         "calls the result undefined", (unsigned)box->left, (unsigned)box->right, (unsigned)box->top,
                         (unsigned)box->bottom, (unsigned)box->front, (unsigned)box->back, what, (unsigned)g->l.width,
                         (unsigned)g->l.height, (unsigned)g->l.depth);
    uint32_t k = g->block;
    if (box->left % k || box->top % k || (box->right % k && box->right != g->l.width) || (box->bottom % k && box->bottom != g->l.height))
        RT_UNIMPLEMENTED(ctx, "a box (%u to %u, %u to %u) of the %s that does not lie on the grid of its format's %u by %u blocks",
                         (unsigned)box->left, (unsigned)box->right, (unsigned)box->top, (unsigned)box->bottom, what, (unsigned)k, (unsigned)k);
    s->x = box->left / k; s->w = (box->right + k - 1) / k - s->x;
    s->y = box->top / k;  s->h = (box->bottom + k - 1) / k - s->y;
    s->z = box->front;    s->d = box->back - box->front;
    return true;
}

static bool whole_of(const Geo *g, const Span *s) {
    return !s->x && !s->y && !s->z && s->w == g->cols && s->h == g->l.rows && s->d == g->l.depth;
}

static UINT usage_of(void *resource) {
    NdTexture *t = texture_of(resource);
    if (!t) return ((NdBuffer *)rt_com_state_of(resource, &rt_nulldev_buffer_class))->desc.Usage;
    Shape s;
    shape_of(t->dim, &t->desc, &s);
    return s.usage;
}

void rt_nulldev_copy(Ctx *ctx, void *dst, UINT dst_sub, UINT x, UINT y, UINT z, void *src, UINT src_sub, const D3D11_BOX *box) {
    Geo gs, gd;
    Span from, to;
    dst = copy_arg(ctx, dst, "destination");
    src = copy_arg(ctx, src, "source");
    geo_of(ctx, src, src_sub, "source", &gs);
    geo_of(ctx, dst, dst_sub, "destination", &gd);
    NdMemory *ms = rt_nulldev_memory(src), *md = rt_nulldev_memory(dst);
    if (gs.buffer != gd.buffer || (!gs.buffer && texture_of(src)->dim != texture_of(dst)->dim))
        RT_UNIMPLEMENTED(ctx, "a copy between resources of different type (%s to %s)", rt_com_class_of(src)->name, rt_com_class_of(dst)->name);
    if (gs.unit != gd.unit || gs.block != gd.block)
        RT_UNIMPLEMENTED(ctx, "a copy between formats whose elements differ in size (%u bytes for %u by %u, to %u bytes for %u by %u)",
                         (unsigned)gs.unit, (unsigned)gs.block, (unsigned)gs.block, (unsigned)gd.unit, (unsigned)gd.block, (unsigned)gd.block);
    /* Unit d3d-fix-3, findings 8 and 9 of docs/reviews/night-2026-10-11-code-review.md.
     * Microsoft, CopyResource (read 2026-10-11): source and destination "Must have compatible DXGI
     * formats, which means the formats must be identical or at least from the same type group.
     * For example, a DXGI_FORMAT_R32G32B32_FLOAT texture can be copied to a
     * DXGI_FORMAT_R32G32B32_UINT texture since both of these formats are in the
     * DXGI_FORMAT_R32G32B32_TYPELESS group.  CopyResource can copy between a few format types.
     * For more info, see Format Conversion using Direct3D 10.1."; and they "Can't be currently
     * mapped."  The page does not say what a device does with a copy that breaks either rule,
     * so each is a stop.  The "few format types" of the 10.1 page (not read) are between a
     * block-compressed and a plain format, which the size check above stops, and, from memory,
     * R9G9B9E5 with R32_UINT, which is a stop here: not built, none recorded.
     * A mapped destination is the case that matters to this device: the game would go on
     * reading, through the pointer it holds, bytes the copy has replaced. */
    if (!gs.buffer) {
        Shape ss, sd;
        shape_of(texture_of(src)->dim, &texture_of(src)->desc, &ss);
        shape_of(texture_of(dst)->dim, &texture_of(dst)->desc, &sd);
        if (rt_nulldev_format_family(ss.format) != rt_nulldev_format_family(sd.format))
            RT_UNIMPLEMENTED(ctx, "a copy between formats of different type groups (%u to %u): Microsoft requires formats that are "
                             "identical or of one group", (unsigned)ss.format, (unsigned)sd.format);
    }
    if (md->mapped)
        RT_UNIMPLEMENTED(ctx, "a copy into a resource that is currently mapped (%u of its %u subresources; subresource %u with map type %u): "
                         "Microsoft says it cannot be", (unsigned)md->mapped, (unsigned)md->nsub, (unsigned)dst_sub,
                         md->blocks ? (unsigned)md->blocks[dst_sub].map_type : 0u);
    if (ms->mapped)
        RT_UNIMPLEMENTED(ctx, "a copy from a resource that is currently mapped (%u of its %u subresources; subresource %u with map type %u): "
                         "Microsoft says it cannot be", (unsigned)ms->mapped, (unsigned)ms->nsub, (unsigned)src_sub,
                         ms->blocks ? (unsigned)ms->blocks[src_sub].map_type : 0u);
    if (usage_of(dst) == D3D11_USAGE_IMMUTABLE) RT_UNIMPLEMENTED(ctx, "a copy into an immutable resource");
    if (md == ms && dst_sub == src_sub) RT_UNIMPLEMENTED(ctx, "a copy of subresource %u of a resource onto itself", (unsigned)src_sub);
    if (!span_of(ctx, &gs, box, "source", &from)) return;                /* an empty box: a no-op */
    if (x % gd.block || y % gd.block)
        RT_UNIMPLEMENTED(ctx, "a copy to (%u, %u), which is not on the grid of the format's %u by %u blocks", (unsigned)x, (unsigned)y,
                         (unsigned)gd.block, (unsigned)gd.block);
    to = (Span){ x / gd.block, y / gd.block, z, from.w, from.h, from.d };
    if ((uint64_t)to.x + to.w > gd.cols || (uint64_t)to.y + to.h > gd.l.rows || (uint64_t)to.z + to.d > gd.l.depth)
        RT_UNIMPLEMENTED(ctx, "a copy of %u by %u by %u units to (%u, %u, %u) of a destination subresource of %u by %u by %u: outside "
                         "it, which Microsoft calls undefined", (unsigned)from.w, (unsigned)from.h, (unsigned)from.d, (unsigned)to.x,
                         (unsigned)to.y, (unsigned)to.z, (unsigned)gd.cols, (unsigned)gd.l.rows, (unsigned)gd.l.depth);
    unsigned kind = rt_nulldev_content(src, src_sub);
    if (kind == ND_CONTENT_DROPPED || kind == ND_CONTENT_RENDERER) {    /* no byte moves; the destination is tainted */
        content_put(dst, md, dst_sub, &gd, &to, kind);
        return;
    }
    /* The source is NONE, GAME or PART.  Where both sides keep bytes they are copied whatever the
     * record says: what the game wrote through a mapped pointer is in the block although the
     * device cannot say which bytes those are, and bytes nobody may read harm nobody. */
    NdBlock *sb = ms->kept ? &ms->blocks[src_sub] : NULL;
    if (md->kept) {
        uint8_t *d = rt_nulldev_block(dst, dst_sub)->data;
        const uint8_t *f = sb ? sb->data : NULL;        /* NULL: no block was ever made, so nothing was written */
        for (uint32_t k = 0; k < to.d; k++)
            for (uint32_t j = 0; j < to.h; j++) {
                uint8_t *row = d + (to.z + k) * gd.l.slice_bytes + (uint64_t)(to.y + j) * gd.l.row_bytes + (uint64_t)to.x * gd.unit;
                if (f) memmove(row, f + (from.z + k) * gs.l.slice_bytes + (uint64_t)(from.y + j) * gs.l.row_bytes + (uint64_t)from.x * gs.unit,
                               (size_t)to.w * gd.unit);
                else memset(row, 0, (size_t)to.w * gd.unit);
            }
    }
    if (kind != ND_CONTENT_PART) {
        /* all of the source box is the game's, or none of it; the game's bytes on a side that
           does not keep them are DROPPED */
        content_put(dst, md, dst_sub, &gd, &to, kind == ND_CONTENT_GAME && !md->kept ? ND_CONTENT_DROPPED : kind);
        return;
    }
    /* PART (so the source keeps bytes): unit by unit.  First the whole box as unwritten, then
     * each run of the source's written units as the game's. */
    bool any = false;
    content_put(dst, md, dst_sub, &gd, &to, ND_CONTENT_NONE);
    for (uint32_t k = 0; k < from.d; k++)
        for (uint32_t j = 0; j < from.h; j++) {
            uint64_t base = ((uint64_t)(from.z + k) * gs.l.rows + from.y + j) * gs.cols + from.x;
            for (uint32_t i = 0; i < from.w; ) {
                /* (the source's bitmap is another subresource's than the destination's, which
                   content_put may make or free: a copy of a subresource onto itself stopped above) */
                if (!mask_bit(sb, base + i)) { i++; continue; }
                uint32_t run = 1;
                while (i + run < from.w && mask_bit(sb, base + i + run)) run++;
                Span part = { to.x + i, to.y + j, to.z + k, run, 1, 1 };
                any = true;
                if (md->kept) content_put(dst, md, dst_sub, &gd, &part, ND_CONTENT_GAME);
                i += run;
            }
        }
    if (any && !md->kept) content_put(dst, md, dst_sub, &gd, &to, ND_CONTENT_DROPPED);
}

void rt_nulldev_copy_all(Ctx *ctx, void *dst, void *src) {
    dst = copy_arg(ctx, dst, "destination");
    src = copy_arg(ctx, src, "source");
    NdMemory *ms = rt_nulldev_memory(src), *md = rt_nulldev_memory(dst);
    if (dst == src) RT_UNIMPLEMENTED(ctx, "CopyResource of a resource onto itself");
    if (rt_com_class_of(src) != rt_com_class_of(dst))
        RT_UNIMPLEMENTED(ctx, "a copy between resources of different type (%s to %s)", rt_com_class_of(src)->name, rt_com_class_of(dst)->name);
    if (ms->nsub != md->nsub)
        RT_UNIMPLEMENTED(ctx, "CopyResource between resources of %u and %u subresources", (unsigned)ms->nsub, (unsigned)md->nsub);
    for (UINT sub = 0; sub < ms->nsub; sub++) {
        Geo gs, gd;
        geo_of(ctx, src, sub, "source", &gs);
        geo_of(ctx, dst, sub, "destination", &gd);
        if (gs.l.width != gd.l.width || gs.l.height != gd.l.height || gs.l.depth != gd.l.depth)
            RT_UNIMPLEMENTED(ctx, "CopyResource between resources of different dimensions (subresource %u: %u by %u by %u to %u by %u by %u)",
                             (unsigned)sub, (unsigned)gs.l.width, (unsigned)gs.l.height, (unsigned)gs.l.depth, (unsigned)gd.l.width,
                             (unsigned)gd.l.height, (unsigned)gd.l.depth);
        rt_nulldev_copy(ctx, dst, sub, 0, 0, 0, src, sub, NULL);
    }
}

void rt_nulldev_update(Ctx *ctx, void *dst, UINT dst_sub, const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch) {
    Geo g;
    Span to;
    dst = copy_arg(ctx, dst, "destination");
    geo_of(ctx, dst, dst_sub, "destination", &g);
    NdMemory *m = rt_nulldev_memory(dst);
    UINT usage = usage_of(dst);
    NdTexture *t = texture_of(dst);
    if (usage == D3D11_USAGE_IMMUTABLE || usage == D3D11_USAGE_DYNAMIC)
        RT_UNIMPLEMENTED(ctx, "UpdateSubresource of a resource created with immutable or dynamic usage (%u)", (unsigned)usage);
    if (!data) RT_UNIMPLEMENTED(ctx, "UpdateSubresource with NULL data");
    if (!span_of(ctx, &g, box, "destination", &to)) return;             /* an empty box: a no-op */
    bool rows_apart = t && t->dim != D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    uint64_t row = (uint64_t)to.w * g.unit, pitch = rows_apart ? row_pitch : row, slice = rows_apart ? depth_pitch : row;
    if (to.h > 1 && pitch < row)
        RT_UNIMPLEMENTED(ctx, "UpdateSubresource with SrcRowPitch %u, smaller than a row of %llu bytes (%u rows)", (unsigned)row_pitch,
                         (unsigned long long)row, (unsigned)to.h);
    if (to.d > 1 && slice < pitch * (to.h - 1) + row)
        RT_UNIMPLEMENTED(ctx, "UpdateSubresource with SrcDepthPitch %u, smaller than a slice of %u rows %llu bytes apart",
                         (unsigned)depth_pitch, (unsigned)to.h, (unsigned long long)pitch);
    uint8_t *d = m->kept ? rt_nulldev_block(dst, dst_sub)->data : NULL;
    for (uint32_t k = 0; k < to.d; k++)
        for (uint32_t j = 0; j < to.h; j++) {
            const uint8_t *from = (const uint8_t *)data + k * slice + j * pitch;
            if (d) memcpy(d + (to.z + k) * g.l.slice_bytes + (uint64_t)(to.y + j) * g.l.row_bytes + (uint64_t)to.x * g.unit, from, (size_t)row);
            else touch(from, row);
        }
    if (!d) rt_nulldev_account(0, 0, 0, (int64_t)(row * to.h * to.d));
    content_put(dst, m, dst_sub, &g, &to, m->kept ? ND_CONTENT_GAME : ND_CONTENT_DROPPED);
}

uint64_t rt_nulldev_ticks(void) { return rt_time_mono_100ns(); }
