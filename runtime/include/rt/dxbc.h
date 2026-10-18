/* The Direct3D shader container ("DXBC"), read as far as the do-nothing device needs it: the
 * header, the chunk table, the checksum and the input signature (unit d3d-reflect;
 * runtime/nulldev/dxbc.c).  The map is docs/research/dxbc-shaders.md, sections 1.1 to 1.4, which
 * names its source for every field; "research 1.x" below points there.
 *
 * Nothing here compiles, translates or prints a shader: the instruction stream (the SHDR or
 * SHEX chunk) is never read.  It is hashed, with every other byte, by the checksum.
 *
 * EVERY LENGTH AND OFFSET IS CHECKED AGAINST THE SIZE THE CALLER PASSED BEFORE IT IS USED.  A
 * container that fails a check is not read further: the function returns false and `why` says
 * which check failed, with the numbers.  The callers (D3DReflect, D3DGetBlobPart) make a stop
 * of it; none of them guesses.
 *
 * THE CHECKS of rt_dxbc_open, in this order:
 *   1  at least 32 bytes (the header; research 1.1) and less than 2^29 (below that the two
 *      published formulations of the checksum's length words agree)
 *   2  begins "DXBC"
 *   3  the version dword is 1 (Microsoft's parser and vkd3d both refuse any other; research 1.1)
 *   4  the size the header declares is the size the caller passed.  Microsoft's parser
 *      (CDXBCParser::ReadDXBC) requires them equal; vkd3d does not look; what d3dcompiler_46
 *      does with a longer buffer is not settled, so anything else is refused
 *   5  the checksum (rt_dxbc_checksum) is the one stored at bytes 4 to 19.  vkd3d, which Wine's
 *      D3DReflect goes through, refuses a wrong one (research 1.2); whether Windows' own
 *      library does is not settled (the blob one byte short that Wine's test gives Windows is
 *      refused, which check 4 explains as well; research 3.1).  It is verified here because it
 *      is the one check that the bytes are the ones Microsoft's compiler wrote: the game
 *      unpacks its shaders with lifted code, and a wrong byte anywhere stops here instead of
 *      becoming a wrong vertex layout later.  One MD5 over a few kilobytes per call
 *   6  the offset table (4 bytes per chunk) fits
 *   7  every chunk's 8-byte header lies after the table and inside the container, and its data
 *      ends inside the container.  Chunks need not be contiguous or in order (Microsoft's
 *      parser requires that, vkd3d does not; nothing settled which Windows' library does)
 */
#ifndef RT_DXBC_H
#define RT_DXBC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RT_DXBC_TAG(a, b, c, d) ((uint32_t)(uint8_t)(a) | (uint32_t)(uint8_t)(b) << 8 | (uint32_t)(uint8_t)(c) << 16 | (uint32_t)(uint8_t)(d) << 24)
#define RT_DXBC_ISGN  RT_DXBC_TAG('I', 'S', 'G', 'N')   /* input signature, 24-byte elements */
#define RT_DXBC_ISG1  RT_DXBC_TAG('I', 'S', 'G', '1')   /* input signature, 32-byte elements: a stream before, a minimum precision after */

#define RT_DXBC_HEADER_BYTES  32u
#define RT_DXBC_WHY           200       /* bytes a caller gives for the reason of a refusal */

/* An opened container: the caller's bytes (not copied) and what the header says.  Valid while
 * the caller's bytes are. */
typedef struct RtDxbc {
    const uint8_t *bytes;
    uint32_t       size;
    uint32_t       nchunks;
} RtDxbc;

/* The container checksum of bytes[0 .. size): MD5 over bytes 20 to the end with a final block
 * of its own (research 1.2).  `size` is at least 20. */
void rt_dxbc_checksum(const void *bytes, size_t size, uint8_t out[16]);

/* Open: the checks above.  True and *c filled, or false and `why` (RT_DXBC_WHY bytes) says
 * which check failed. */
bool rt_dxbc_open(const void *bytes, uint64_t size, RtDxbc *c, char *why);

/* Chunk i of an opened container (i < c->nchunks): its tag, its data and the data's length.
 * The data lies inside the container (check 7). */
void rt_dxbc_chunk(const RtDxbc *c, uint32_t i, uint32_t *tag, const uint8_t **data, uint32_t *len);

/* The four characters of a tag for a message, a byte that is not printable as '?'. */
void rt_dxbc_tag_text(uint32_t tag, char out[5]);

/* One element of a signature (research 1.4), as the chunk has it: nothing is adjusted. */
typedef struct RtDxbcElement {
    const char *name;               /* the semantic name, inside the chunk's data; its NUL is inside too */
    uint32_t    name_length;        /* without the NUL */
    uint32_t    semantic_index;
    uint32_t    system_value;       /* D3D_NAME */
    uint32_t    component_type;     /* D3D_REGISTER_COMPONENT_TYPE: 0 to 3 */
    uint32_t    reg;
    uint8_t     mask;               /* the components the element occupies */
    uint8_t     rw_mask;            /* of an input: the components the shader always reads */
    uint32_t    stream;             /* ISG1's first field; 0 for ISGN */
    uint32_t    min_precision;      /* ISG1's last field (D3D_MIN_PRECISION); 0 for ISGN */
} RtDxbcElement;

/* The number of elements of an input-signature chunk (tag ISGN or ISG1; data and len as
 * rt_dxbc_chunk gave them).  Checks: the tag is one of the two; the chunk has its 8-byte
 * header (count, offset of the element array); the offset is at least 8 and inside the chunk
 * (Microsoft's parser goes to that offset; every writer puts 8 there); `count` elements of
 * the tag's size fit between the offset and the chunk's end.  False and `why` otherwise. */
bool rt_dxbc_signature_count(uint32_t tag, const uint8_t *data, uint32_t len, uint32_t *count, char *why);

/* Element i (i below the count rt_dxbc_signature_count gave for the same chunk).  Checks: the
 * name's offset lies inside the chunk and the name ends (a NUL) inside it; the component
 * type is one of D3D_REGISTER_COMPONENT_TYPE's four (vkd3d refuses any other; a shader of
 * model 4 or 5 has no other).  False and `why` otherwise. */
bool rt_dxbc_signature_element(uint32_t tag, const uint8_t *data, uint32_t len, uint32_t i, RtDxbcElement *out, char *why);

#endif /* RT_DXBC_H */
