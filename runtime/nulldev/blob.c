/* The blob of the do-nothing device: what D3DGetBlobPart hands out (unit d3d-reflect;
 * rt/nulldev.h, NdBlob).  One kind is made: the input-signature part of a shader container.
 *
 * THE PART.  A container of its own that holds the source's ISGN chunk and nothing else:
 *     "DXBC", checksum, version 1, total size, chunk count 1, the chunk's offset (36),
 *     the chunk as it lies in the source: "ISGN", its data length, its data
 * so 44 bytes more than the chunk's data.  Sources: Wine's test_get_blob_part, run on Windows
 * (docs/research/dxbc-shaders.md, 3.9: the part is a container with ISGN at dword 9, and
 * asking that container for its input signature succeeds again), and the recording of the
 * real game: every CreateInputLayout there passes such a blob, and its length is 44 more than
 * a signature chunk of the layout's semantic names would have (docs/trace/d3d-creators.md: 88
 * bytes for POSITION alone, 120 for POSITION and TEXCOORD; docs/handoff/d3d-reflect.md works
 * them out).  The checksum is computed (rt_dxbc_checksum), so the part opens as a container.
 *
 * WHICH CHUNK.  Exactly one ISGN.  Wine maps this part to ISGN only and needs exactly one;
 * what Windows gives for a container whose input signature is an ISG1 chunk is not settled
 * (research 3.9), so that, none and two are refused. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/log.h"
#include "rt/nulldev.h"

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

ID3D10Blob *rt_nulldev_signature_blob_new(const void *bytes, SIZE_T size, char *why) {
    RtDxbc c;
    if (!rt_dxbc_open(bytes, size, &c, why)) return NULL;

    uint32_t len = 0, found = 0, wide = 0;
    const uint8_t *data = NULL;
    for (uint32_t i = 0; i < c.nchunks; i++) {
        uint32_t t, l;
        const uint8_t *d;
        rt_dxbc_chunk(&c, i, &t, &d, &l);
        if (t == RT_DXBC_ISG1) wide++;
        if (t != RT_DXBC_ISGN) continue;
        if (!found++) { data = d; len = l; }
    }
    if (found != 1) {
        snprintf(why, RT_DXBC_WHY, "it has %u ISGN chunks (and %u ISG1) among its %u chunks; the input-signature part is made of exactly one ISGN",
                 found, wide, c.nchunks);
        return NULL;
    }

    uint32_t total = RT_DXBC_HEADER_BYTES + 4 + 8 + len;        /* len is less than the source's size, which is below 2^29 */
    uint8_t *part = calloc(1, total);
    if (!part) rt_fatal("rt_nulldev: no host memory for a blob of %u bytes", total);
    memcpy(part, "DXBC", 4);
    put32(part + 20, 1);
    put32(part + 24, total);
    put32(part + 28, 1);
    put32(part + 32, RT_DXBC_HEADER_BYTES + 4);
    memcpy(part + 36, "ISGN", 4);
    put32(part + 40, len);
    memcpy(part + 44, data, len);
    rt_dxbc_checksum(part, total, part + 4);

    ID3D10Blob *obj = rt_com_new(&rt_nulldev_blob_class);
    NdBlob *b = rt_com_state_of(obj, &rt_nulldev_blob_class);
    b->data = part;
    b->size = total;
    rt_log(RT_CH_COM, RT_LL_INFO, "blob #%llu: the input-signature part of a container of %u bytes: %u bytes (an ISGN chunk of %u)",
           (unsigned long long)rt_com_serial(obj), c.size, total, len);
    return obj;
}

void rt_nulldev_blob_free(NdBlob *b) {
    free(b->data);
    b->data = NULL;
    b->size = 0;
}
