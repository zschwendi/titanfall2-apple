/* The reader of the Direct3D shader container (rt/dxbc.h has the contract and the checks; the
 * layout is docs/research/dxbc-shaders.md, sections 1.1 to 1.4, field by field with sources).
 * All values are little-endian and are read byte by byte, so nothing depends on alignment. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rt/dxbc.h"

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static bool refuse(char *why, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static bool refuse(char *why, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, RT_DXBC_WHY, fmt, ap);
    va_end(ap);
    return false;
}

void rt_dxbc_tag_text(uint32_t tag, char out[5]) {
    for (int i = 0; i < 4; i++) {
        uint8_t ch = (uint8_t)(tag >> (8 * i));
        out[i] = ch >= 0x20 && ch < 0x7f ? (char)ch : '?';
    }
    out[4] = 0;
}

/* ---- the checksum (research 1.2) --------------------------------------------------------------------
 * MD5's compression function is the standard one (RFC 1321: the constants are
 * floor(2^32 * |sin(i + 1)|), the four rounds and their shifts as below); only the last block
 * differs from MD5. */
static void md5_block(uint32_t s[4], const uint8_t b[64]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const uint8_t R[4][4] = { { 7, 12, 17, 22 }, { 5, 9, 14, 20 }, { 4, 11, 16, 23 }, { 6, 10, 15, 21 } };
    uint32_t m[16], a = s[0], bb = s[1], c = s[2], d = s[3];
    for (int i = 0; i < 16; i++) m[i] = u32(b + 4 * i);
    for (unsigned i = 0; i < 64; i++) {
        uint32_t f;
        unsigned g;
        switch (i >> 4) {
        case 0:  f = (bb & c) | (~bb & d); g = i;                break;
        case 1:  f = (d & bb) | (~d & c);  g = (5 * i + 1) & 15; break;
        case 2:  f = bb ^ c ^ d;           g = (3 * i + 5) & 15; break;
        default: f = c ^ (bb | ~d);        g = (7 * i) & 15;     break;
        }
        f += a + K[i] + m[g];
        a = d; d = c; c = bb;
        unsigned r = R[i >> 4][i & 3];
        bb += f << r | f >> (32 - r);
    }
    s[0] += a; s[1] += bb; s[2] += c; s[3] += d;
}

void rt_dxbc_checksum(const void *bytes, size_t size, uint8_t out[16]) {
    const uint8_t *p = (const uint8_t *)bytes + 20;             /* the magic and the checksum itself are not hashed */
    size_t len = size - 20, left = len & 63;
    uint32_t s[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    uint32_t bits = (uint32_t)(len << 3), last = bits >> 2 | 1; /* only the low 32 bits of the bit count are used */
    uint8_t blk[64];

    for (size_t n = len >> 6; n; n--, p += 64) md5_block(s, p);
    memset(blk, 0, sizeof blk);
    if (left >= 56) {
        /* The leftover bytes, 0x80 and zeroes are one ordinary block; the two length words get a block of their own. */
        memcpy(blk, p, left);
        blk[left] = 0x80;
        md5_block(s, blk);
        memset(blk, 0, sizeof blk);
        put32(blk, bits);
    } else {
        /* The bit count first, then the leftover bytes, 0x80, zeroes up to byte 59. */
        put32(blk, bits);
        memcpy(blk + 4, p, left);
        blk[4 + left] = 0x80;
    }
    put32(blk + 60, last);
    md5_block(s, blk);
    for (int i = 0; i < 4; i++) put32(out + 4 * i, s[i]);
}

/* ---- the header and the chunk table (research 1.1) ------------------------------------------------ */

bool rt_dxbc_open(const void *bytes, uint64_t size, RtDxbc *c, char *why) {
    const uint8_t *b = bytes;
    uint8_t sum[16];

    if (size < RT_DXBC_HEADER_BYTES)
        return refuse(why, "%llu bytes: shorter than the 32-byte header", (unsigned long long)size);
    if (size >= UINT64_C(0x20000000))
        return refuse(why, "%llu bytes: 2^29 or more, where the checksum's length words are not settled", (unsigned long long)size);
    if (memcmp(b, "DXBC", 4))
        return refuse(why, "it does not begin with DXBC (its first four bytes are %02x %02x %02x %02x)", b[0], b[1], b[2], b[3]);
    if (u32(b + 20) != 1)
        return refuse(why, "container version %#x, not 1", u32(b + 20));
    if (u32(b + 24) != size)
        return refuse(why, "the header declares %u bytes and the caller passed %llu", u32(b + 24), (unsigned long long)size);
    rt_dxbc_checksum(b, (size_t)size, sum);
    if (memcmp(sum, b + 4, 16))
        return refuse(why, "the checksum of its %llu bytes begins %02x%02x%02x%02x and the header stores %02x%02x%02x%02x: "
                      "these are not the bytes the compiler wrote", (unsigned long long)size, sum[0], sum[1], sum[2], sum[3],
                      b[4], b[5], b[6], b[7]);

    uint32_t n = u32(b + 28), total = (uint32_t)size;
    if (n > (total - RT_DXBC_HEADER_BYTES) / 4)
        return refuse(why, "%u chunks: their offset table does not fit in the container's %u bytes", n, total);
    uint32_t first = RT_DXBC_HEADER_BYTES + 4 * n;              /* the first byte after the table */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = u32(b + RT_DXBC_HEADER_BYTES + 4 * i);
        if (off < first)
            return refuse(why, "chunk %u at offset %u lies inside the header and offset table (%u bytes)", i, off, first);
        if (off > total || total - off < 8)
            return refuse(why, "chunk %u at offset %u: its 8-byte header runs past the container's %u bytes", i, off, total);
        uint32_t len = u32(b + off + 4);
        if (len > total - off - 8) {
            char tag[5];
            rt_dxbc_tag_text(u32(b + off), tag);
            return refuse(why, "chunk %u (%s) at offset %u: its data of %u bytes runs past the container's %u bytes", i, tag, off, len, total);
        }
    }
    c->bytes = b;
    c->size = total;
    c->nchunks = n;
    return true;
}

void rt_dxbc_chunk(const RtDxbc *c, uint32_t i, uint32_t *tag, const uint8_t **data, uint32_t *len) {
    uint32_t off = u32(c->bytes + RT_DXBC_HEADER_BYTES + 4 * i);
    *tag = u32(c->bytes + off);
    *len = u32(c->bytes + off + 4);
    *data = c->bytes + off + 8;
}

/* ---- the input signature (research 1.4) -------------------------------------------------------------
 * The chunk's data: element count, offset of the element array, the elements, the names.
 *     ISGN element, 24 bytes:  name offset, semantic index, system value, component type,
 *                              register (five dwords), mask (byte), read mask (byte), 2 bytes of padding
 *     ISG1 element, 32 bytes:  stream (dword), the 24 bytes above, minimum precision (dword)
 * A name's offset counts from the start of the chunk's data. */

static bool form(uint32_t tag, uint32_t *stride, uint32_t *core) {
    if (tag == RT_DXBC_ISGN) { *stride = 24; *core = 0; return true; }
    if (tag == RT_DXBC_ISG1) { *stride = 32; *core = 4; return true; }
    return false;
}

bool rt_dxbc_signature_count(uint32_t tag, const uint8_t *data, uint32_t len, uint32_t *count, char *why) {
    uint32_t stride, core;
    char text[5];
    rt_dxbc_tag_text(tag, text);
    if (!form(tag, &stride, &core)) return refuse(why, "%s is not an input-signature chunk (ISGN or ISG1)", text);
    if (len < 8) return refuse(why, "the %s chunk has %u bytes: shorter than its 8-byte header", text, len);
    uint32_t n = u32(data), off = u32(data + 4);
    if (off < 8 || off > len)
        return refuse(why, "the %s chunk of %u bytes puts its element array at offset %u", text, len, off);
    if (n > (len - off) / stride)
        return refuse(why, "the %s chunk's %u elements of %u bytes at offset %u do not fit in its %u bytes", text, n, stride, off, len);
    *count = n;
    return true;
}

bool rt_dxbc_signature_element(uint32_t tag, const uint8_t *data, uint32_t len, uint32_t i, RtDxbcElement *out, char *why) {
    uint32_t stride = 0, core = 0;
    char text[5];
    rt_dxbc_tag_text(tag, text);
    (void)form(tag, &stride, &core);
    const uint8_t *e = data + u32(data + 4) + (size_t)i * stride, *f = e + core;
    uint32_t name = u32(f);
    if (name >= len)
        return refuse(why, "%s element %u: its name at offset %u lies outside the chunk's %u bytes", text, i, name, len);
    const uint8_t *end = memchr(data + name, 0, len - name);
    if (!end)
        return refuse(why, "%s element %u: its name at offset %u does not end inside the chunk's %u bytes", text, i, name, len);
    if (u32(f + 12) > 3)
        return refuse(why, "%s element %u: component type %u is none of D3D_REGISTER_COMPONENT_TYPE's four", text, i, u32(f + 12));
    out->name = (const char *)(data + name);
    out->name_length = (uint32_t)(end - (data + name));
    out->semantic_index = u32(f + 4);
    out->system_value = u32(f + 8);
    out->component_type = u32(f + 12);
    out->reg = u32(f + 16);
    out->mask = f[20];
    out->rw_mask = f[21];
    out->stream = core ? u32(e) : 0;
    out->min_precision = core ? u32(e + 28) : 0;
    return true;
}
