/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map
 * RT-CASES:  WRITE_DISCARD of a dynamic buffer: S_OK, a 16-byte aligned pointer, both pitches the buffer's size; the block ends at a guard page; what the game wrote is there at the next mapping; a read mapping of a staging buffer returns its initial data; a mip level of a texture 2D gets the pitches of that level; a BC1 texture of 10 by 6 has RowPitch 24 and DepthPitch 48; WRITE_NO_OVERWRITE of a dynamic vertex buffer keeps its bytes; what the game writes through the pointer of a writing Map is kept and is not counted as written
 * RT-TRAPS:  Map of a NULL resource; Map with a NULL pMappedResource; Map with flags; Map of a subresource the resource does not have; Map of a multisampled texture; Map of a subresource that is already mapped; a map type that is not one of D3D11_MAP; D3D11_MAP_READ of a resource created without D3D11_CPU_ACCESS_READ; of a resource created without D3D11_CPU_ACCESS_WRITE; D3D11_MAP_WRITE of a dynamic resource; D3D11_MAP_WRITE_DISCARD of a resource that is not dynamic; D3D11_MAP_WRITE_NO_OVERWRITE of anything but a dynamic vertex or index buffer; a read mapping of a subresource no call wrote; a read mapping of a subresource of which calls wrote only a part; a read mapping of a subresource that a copy from a renderer's output or a resolve wrote; whose contents are the game's own bytes, copied from a resource whose bytes the device did not keep; does not fit DepthPitch's 32 bits
 */
#include <string.h>

#include "rt/nulldev.h"

/* WHAT THE GAME FINDS IN THE MEMORY, per map type (the brief's item 4; the hand-off has the
 * table).  The device computes no contents, so a block holds exactly what the game put there
 * through initial data and earlier mappings, or zeroes where it put nothing:
 *   WRITE_DISCARD        the block as it is.  D3D11_MAP: "the previous contents of the resource
 *                        will be undefined", so whatever is there is a correct answer.
 *   WRITE_NO_OVERWRITE   the block as it is: what the game wrote before, which it promises
 *                        not to overwrite.
 *   WRITE                the block as it is (the game must not read it: the Map page, "Don't
 *                        read from a subresource mapped for writing").
 *   READ, READ_WRITE     what the game itself wrote, and only that, by the subresource's record
 *                        (rt/nulldev.h, ND_CONTENT_*; unit d3d-fix-2): GAME, which includes bytes
 *                        a copy brought from another block the device holds, is handed out; a
 *                        STOP when no call wrote it (NONE) or calls wrote only a part of it
 *                        (PART; unit d3d-fix-3), when a copy from a renderer's output or a
 *                        resolve wrote it (RENDERER), and when the bytes are the game's but the
 *                        device did not keep them (DROPPED).  Never invented bytes.
 * WHAT COUNTS AS WRITTEN (unit d3d-fix-3; the night's review, finding 3): initial data,
 * UpdateSubresource and a copy from the game's bytes, unit by unit.  NOT what the game writes
 * through the pointer of a writing mapping: the device cannot see which bytes those are, and
 * the ones the game leaves are what D3D11_MAP calls undefined (WRITE_DISCARD: "the previous
 * contents of the resource will be undefined") or what was there before.  So a staging
 * resource filled only through Map(WRITE) is a stop at Map(READ), also when the game filled
 * all of it: the stop errs towards saying so.
 * The block ends at a guard page (rt_nulldev_block): a write past the subresource, rounded up
 * to 16 bytes, faults at the instruction that made it.
 *
 * WHAT MICROSOFT DOCUMENTS AS INVALID (D3D11_MAP, read 2026-10-10 by a subagent that returned
 * the text): READ "must have been created with read access (see D3D11_CPU_ACCESS_READ)"; WRITE
 * "with write access"; READ_WRITE "with read and write access"; WRITE_DISCARD "with write
 * access and dynamic usage"; WRITE_NO_OVERWRITE "is only valid on vertex and index buffers ...
 * Cannot be used on a resource created with the D3D11_BIND_CONSTANT_BUFFER flag", and "cannot
 * be used with dynamic textures".  The Map page names no error code for any of them ("returns
 * one of the Direct3D 11 Return Codes").  WHAT WINDOWS DOES (Wine's d3d11 tests, as read by
 * docs/reviews/d3d-contract-check.md, E6: check_resource_cpu_access_, test_resource_map): with
 * R and W the CPU access flags and D dynamic usage, READ succeeds iff R, WRITE iff W and not D,
 * READ_WRITE iff R and W, WRITE_DISCARD iff D, WRITE_NO_OVERWRITE iff D and a buffer; a refusal
 * and a subresource index past the last are E_INVALIDARG with pData NULL.  Each refusal is a
 * STOP here that prints the combination, as E6 recommends: the game tests 10 of its 71 Map
 * results (design 3.2), so an error with a NULL pointer would mostly become a write to
 * address 0, and a refusal can only mean that something upstream of the call is already
 * wrong.  WRITE_NO_OVERWRITE on a dynamic buffer that is not a vertex or index buffer is a
 * stop too: Microsoft's page and Wine's table disagree about it.  Not in either source, and
 * stops: a second mapping of a mapped subresource, a NULL resource.  A NULL
 * pMappedResource is documented only for ID3D11DeviceContext3 and DEFAULT textures: a stop.
 * MapFlags: all 71 call sites pass 0 (finding 31); D3D11_MAP_FLAG_DO_NOT_WAIT would be
 * harmless here, but no call asks, so any flag is a stop.
 *
 * The pitches: D3D11_MAPPED_SUBRESOURCE's page ("RowPitch contains the value that the runtime
 * adds to pData to move from row to row", DepthPitch "from depth slice to depth slice"; for a
 * buffer of 8 bytes both "greater than or equal to 8"), computed in memory.c. */
COM_METHOD(ID3D11DeviceContext, HRESULT, Map, ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE *pMappedResource)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pResource) RT_UNIMPLEMENTED(ctx, "Map of a NULL resource");
    pResource = rt_nulldev_resource_arg(ctx, pResource, "pResource");
    if (!pMappedResource) RT_UNIMPLEMENTED(ctx, "Map with a NULL pMappedResource");
    if (MapFlags) RT_UNIMPLEMENTED(ctx, "Map with flags 0x%x", (unsigned)MapFlags);
    NdMemory *m = rt_nulldev_memory(pResource);
    NdLevel l;
    if (!rt_nulldev_sub(pResource, Subresource, &l))
        RT_UNIMPLEMENTED(ctx, "Map of a subresource the resource does not have: %u of %u", (unsigned)Subresource, (unsigned)m->nsub);
    if (!m->size) RT_UNIMPLEMENTED(ctx, "Map of a multisampled texture");

    UINT usage, cpu, bind;
    bool is_buffer = rt_com_class_of(pResource) == &rt_nulldev_buffer_class;
    if (is_buffer) {
        const NdBuffer *b = rt_com_state_of(pResource, &rt_nulldev_buffer_class);
        usage = b->desc.Usage; cpu = b->desc.CPUAccessFlags; bind = b->desc.BindFlags;
    } else {
        const NdTexture *t = rt_com_state_of(pResource, rt_com_class_of(pResource));
        usage = t->dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D ? t->desc.d1.Usage : t->dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D ? t->desc.d2.Usage : t->desc.d3.Usage;
        cpu = t->dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D ? t->desc.d1.CPUAccessFlags : t->dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D ? t->desc.d2.CPUAccessFlags : t->desc.d3.CPUAccessFlags;
        bind = 0;
    }
    bool reads = MapType == D3D11_MAP_READ || MapType == D3D11_MAP_READ_WRITE, writes = MapType != D3D11_MAP_READ;
    if (MapType < D3D11_MAP_READ || MapType > D3D11_MAP_WRITE_NO_OVERWRITE)
        RT_UNIMPLEMENTED(ctx, "Map with %u, a map type that is not one of D3D11_MAP", (unsigned)MapType);
    if (reads && !(cpu & D3D11_CPU_ACCESS_READ))
        RT_UNIMPLEMENTED(ctx, "D3D11_MAP_READ of a resource created without D3D11_CPU_ACCESS_READ (map type %u, usage %u, CPU access "
                         "flags 0x%x): documented as invalid, with no error code named", (unsigned)MapType, (unsigned)usage, (unsigned)cpu);
    if (writes && !(cpu & D3D11_CPU_ACCESS_WRITE))
        RT_UNIMPLEMENTED(ctx, "a writing map (type %u) of a resource created without D3D11_CPU_ACCESS_WRITE (usage %u, CPU access "
                         "flags 0x%x): documented as invalid, with no error code named", (unsigned)MapType, (unsigned)usage, (unsigned)cpu);
    if (MapType == D3D11_MAP_WRITE && usage == D3D11_USAGE_DYNAMIC)
        RT_UNIMPLEMENTED(ctx, "D3D11_MAP_WRITE of a dynamic resource: Windows refuses it (E_INVALIDARG)");
    if (MapType == D3D11_MAP_WRITE_DISCARD && usage != D3D11_USAGE_DYNAMIC)
        RT_UNIMPLEMENTED(ctx, "D3D11_MAP_WRITE_DISCARD of a resource that is not dynamic (usage %u): documented as invalid, with "
                         "no error code named", (unsigned)usage);
    if (MapType == D3D11_MAP_WRITE_NO_OVERWRITE &&
        !(is_buffer && usage == D3D11_USAGE_DYNAMIC && (bind & (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER)) &&
          !(bind & D3D11_BIND_CONSTANT_BUFFER)))
        RT_UNIMPLEMENTED(ctx, "D3D11_MAP_WRITE_NO_OVERWRITE of anything but a dynamic vertex or index buffer (usage %u, bind flags "
                         "0x%x): documented as invalid, with no error code named", (unsigned)usage, (unsigned)bind);

    if (l.slice_bytes > UINT32_MAX)
        RT_UNIMPLEMENTED(ctx, "Map of a subresource whose slice of %llu bytes does not fit DepthPitch's 32 bits (%u by %u)",
                         (unsigned long long)l.slice_bytes, (unsigned)l.width, (unsigned)l.height);
    /* THE SEAM (rt/d3dback.h, rule 5), after every check of the call above: with a back end the
     * memory is the back end's.  The front has no block for the resource (NdMemory.kept is 0); it
     * records that the subresource is mapped, and hands the game the back end's pointer and
     * pitches as they are.  Nothing is refused for what the subresource holds: a renderer has the
     * contents the front could only have invented (rule 6), so the ledger counts nothing.  Any
     * result but S_OK with a pointer is a stop (rule 4): the front accepted the call. */
    if (rt_d3dback()) {
        if (m->back_mapped[Subresource])
            RT_UNIMPLEMENTED(ctx, "Map of a subresource that is already mapped (%u, with map type %u)", (unsigned)Subresource,
                             (unsigned)m->back_mapped[Subresource]);
        D3D11_MAPPED_SUBRESOURCE got = { NULL, 0, 0 };
        HRESULT hr = RT_BACK_ANSWER(ctx, Map, c->child.back, rt_d3dback_of(pResource), Subresource, MapType, MapFlags, &got);
        if (hr != S_OK || !got.pData) rt_d3dback_differs(ctx, RT_BACK_Map, S_OK, hr != S_OK ? hr : E_POINTER);
        m->back_mapped[Subresource] = (uint8_t)MapType;
        m->mapped++;
        *pMappedResource = got;
        return S_OK;
    }
    NdBlock *b = rt_nulldev_block(pResource, Subresource);
    if (b->map_type) RT_UNIMPLEMENTED(ctx, "Map of a subresource that is already mapped (%u, with map type %u)", (unsigned)Subresource, (unsigned)b->map_type);
    unsigned content = rt_nulldev_content(pResource, Subresource);
    if (reads && content == ND_CONTENT_RENDERER) {
        /* The one answer here that would be invented.  A stop by default; with RT_D3D_READBACK=zero
         * the game gets zeroes, and the ledger counts each such mapping (rt/nulldev.h). */
        if (!rt_nulldev_readback_zero())
            RT_UNIMPLEMENTED(ctx, "a read mapping of a subresource that a copy from a renderer's output or a resolve wrote: its contents "
                             "are what only a renderer could have produced (subresource %u, %u by %u, %llu bytes; RT_D3D_READBACK=zero "
                             "hands out zeroes instead)", (unsigned)Subresource, (unsigned)l.width, (unsigned)l.height,
                             (unsigned long long)l.bytes);
        memset(b->data, 0, l.bytes);
        rt_nulldev_invented(ctx, ND_INV_READBACK, l.bytes);
    }
    if (reads && content == ND_CONTENT_DROPPED)
        RT_UNIMPLEMENTED(ctx, "a read mapping of a subresource whose contents are the game's own bytes, copied from a resource "
                         "whose bytes the device did not keep (subresource %u, %llu bytes)", (unsigned)Subresource,
                         (unsigned long long)l.bytes);
    if (reads && content == ND_CONTENT_NONE)
        RT_UNIMPLEMENTED(ctx, "a read mapping of a subresource no call wrote (subresource %u, %llu bytes): Windows calls its "
                         "contents undefined, and what the game may have written through the pointer of a writing Map the "
                         "device cannot see, so there is nothing it can vouch for", (unsigned)Subresource, (unsigned long long)l.bytes);
    if (reads && content == ND_CONTENT_PART) {
        uint64_t total, known = rt_nulldev_known_units(pResource, Subresource, &total);
        RT_UNIMPLEMENTED(ctx, "a read mapping of a subresource of which calls wrote only a part (subresource %u: %llu of %llu "
                         "units, %llu bytes in all): Windows calls the rest undefined, and what the game may have written "
                         "through the pointer of a writing Map the device cannot see", (unsigned)Subresource,
                         (unsigned long long)known, (unsigned long long)total, (unsigned long long)l.bytes);
    }
    b->map_type = MapType;
    /* A writing mapping changes the record only when it discards: which bytes the game writes
     * through the pointer the device cannot see (finding 3 of the night's review), so GAME stays
     * GAME (every byte was the game's and still is), and what was not GAME is not made so. */
    if (MapType == D3D11_MAP_WRITE_DISCARD) rt_nulldev_discarded(pResource, Subresource);
    m->mapped++;
    pMappedResource->pData = b->data;
    pMappedResource->RowPitch = l.row_bytes;
    pMappedResource->DepthPitch = (UINT)l.slice_bytes;
    return S_OK;
}
