/* Test support: assemble a Direct3D shader container by hand, from the documented layout
 * (docs/research/dxbc-shaders.md, sections 1.1 and 1.4), for the tests of rt/dxbc.h, of
 * D3DReflect and of the reflection object.  Nothing here is compiler output and none of it
 * is a game file: the "shader" of these containers is a chunk of the test's own filler bytes.
 *
 *     header, 32 bytes:   "DXBC", 16 bytes of checksum, version 1, total size, chunk count
 *     offset table:       one dword per chunk, from the container's start
 *     each chunk:         tag (4 characters), data length, data
 *     a signature chunk's data:  element count, 8 (offset of the element array), the elements,
 *                                the names (NUL-terminated), 0xAB up to a multiple of 4
 *     an ISGN element, 24 bytes: name offset (from the data's start), semantic index, system
 *                                value, component type, register, mask, read mask, 2 bytes 0
 *     an ISG1 element, 32 bytes: stream, the 24 bytes above, minimum precision
 *
 * The checksum is stamped with the runtime's own rt_dxbc_checksum: it makes the INPUT valid
 * and is never an expected value.  That function is held against answers from another
 * implementation in tests/core/dxbc.c.
 *
 * Header-only (static inline), so that a test needs no build rule.  The caller's buffers must
 * be large enough; T_DXBC_MAX is plenty for every container the tests make. */
#ifndef RT_TEST_DXBC_H
#define RT_TEST_DXBC_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "rt/dxbc.h"

#define T_DXBC_MAX 1024

typedef struct TDxbcElement {
    const char *name;
    uint32_t    index, system_value, component_type, reg;
    uint8_t     mask, rw_mask;
    uint32_t    stream, min_precision;      /* written only into an ISG1 element */
} TDxbcElement;

typedef struct TDxbcChunk {
    const char    *tag;                     /* four characters */
    const uint8_t *data;
    uint32_t       len;
} TDxbcChunk;

static inline void t_dxbc_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* The data of a signature chunk for `n` elements (ISG1's form when `wide`); returns its length. */
static inline uint32_t t_dxbc_signature(uint8_t *out, bool wide, const TDxbcElement *e, uint32_t n) {
    uint32_t stride = wide ? 32 : 24, names = 8 + n * stride;
    t_dxbc_put32(out, n);
    t_dxbc_put32(out + 4, 8);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *el = out + 8 + i * stride, *core = el + (wide ? 4 : 0);
        uint32_t len = (uint32_t)strlen(e[i].name) + 1;
        if (wide) { t_dxbc_put32(el, e[i].stream); t_dxbc_put32(el + 28, e[i].min_precision); }
        t_dxbc_put32(core, names);
        t_dxbc_put32(core + 4, e[i].index);
        t_dxbc_put32(core + 8, e[i].system_value);
        t_dxbc_put32(core + 12, e[i].component_type);
        t_dxbc_put32(core + 16, e[i].reg);
        core[20] = e[i].mask;
        core[21] = e[i].rw_mask;
        core[22] = core[23] = 0;
        memcpy(out + names, e[i].name, len);
        names += len;
    }
    while (names & 3) out[names++] = 0xab;
    return names;
}

/* Write the checksum of c[0 .. size) into its header: after assembling, and again after a
 * test changed bytes that a check behind the checksum is to meet. */
static inline void t_dxbc_stamp(uint8_t *c, uint32_t size) {
    rt_dxbc_checksum(c, size, c + 4);
}

/* A container of `n` chunks, one after the other in the order given, its size and checksum
 * stamped; returns its size. */
static inline uint32_t t_dxbc_container(uint8_t *out, const TDxbcChunk *chunks, uint32_t n) {
    uint32_t at = 32 + 4 * n;
    memcpy(out, "DXBC", 4);
    memset(out + 4, 0, 16);
    t_dxbc_put32(out + 20, 1);
    t_dxbc_put32(out + 28, n);
    for (uint32_t i = 0; i < n; i++) {
        t_dxbc_put32(out + 32 + 4 * i, at);
        memcpy(out + at, chunks[i].tag, 4);
        t_dxbc_put32(out + at + 4, chunks[i].len);
        memcpy(out + at + 8, chunks[i].data, chunks[i].len);
        at += 8 + chunks[i].len;
    }
    t_dxbc_put32(out + 24, at);
    t_dxbc_stamp(out, at);
    return at;
}

#endif /* RT_TEST_DXBC_H */
