/* The reader of the Direct3D shader container (rt/dxbc.h, runtime/nulldev/dxbc.c).
 *
 * WHERE THE EXPECTED VALUES COME FROM.  None from the reader.
 *   The checksums: computed by the repository's other implementation of the algorithm, the
 *     Python one of tools/dxmt/make_dxbc.py (dxbc_checksum; written by another unit, which held
 *     it against the stored checksums of two compiler-made containers), over the same bytes:
 *         for size in 20 21 75 76 83 84 85 148 4432:
 *             make_dxbc.dxbc_checksum(bytes((i * 7 + 3) & 0xff for i in range(size)))
 *     The sizes put 0, 1, 55, 56 and 63 bytes after the last whole block of the hashed part
 *     (everything from byte 20 on), on both sides of the 56 at which the final block changes
 *     form (docs/research/dxbc-shaders.md, 1.2).
 *   The containers: assembled by rt/test_dxbc.h from the documented layout (research 1.1,
 *     1.4); every number the reader is expected to give back is one the test put in, and the
 *     offsets and lengths are counted here from that layout.
 *   The refusals: each malformed container is a well-formed one with the one field changed
 *     that the check is about, and its checksum made right again unless the checksum is the
 *     check, so that the check named is the one that meets it. */
#include <string.h>

#include "rt/test.h"
#include "rt/test_dxbc.h"

static const TDxbcElement sig[] = {
    /* name        index  system value  type  register  mask  read   (stream, precision: ISG1 only) */
    { "POSITION",  0,     0,            3,    0,        0x7,  0x7,   0, 0 },
    { "TEXCOORD",  2,     0,            3,    1,        0x3,  0x1,   0, 1 },   /* an index above 0; a mask that is not all four */
    { "SV_VertexID", 0,   6,            1,    2,        0x1,  0x1,   0, 0 },   /* a system value: D3D_NAME_VERTEX_ID, an unsigned integer */
    { "COLOR",     1,     0,            2,    0xffffffff, 0xf, 0x0,  3, 5 },
};
#define NSIG (sizeof sig / sizeof sig[0])

static bool refused(const uint8_t *c, uint64_t size, const char *piece) {
    RtDxbc d;
    char why[RT_DXBC_WHY] = "";
    if (rt_dxbc_open(c, size, &d, why)) return false;
    if (!strstr(why, piece)) rt_test_fail(__FILE__, __LINE__, "the refusal reads \"%s\", which lacks \"%s\"", why, piece);
    return strstr(why, piece) != NULL;
}

static bool sig_refused(uint32_t tag, const uint8_t *data, uint32_t len, const char *piece) {
    char why[RT_DXBC_WHY] = "";
    uint32_t n = 0;
    RtDxbcElement e;
    bool ok = rt_dxbc_signature_count(tag, data, len, &n, why);
    for (uint32_t i = 0; ok && i < n; i++) ok = rt_dxbc_signature_element(tag, data, len, i, &e, why);
    if (ok) return false;
    if (!strstr(why, piece)) rt_test_fail(__FILE__, __LINE__, "the refusal reads \"%s\", which lacks \"%s\"", why, piece);
    return strstr(why, piece) != NULL;
}

TEST(core, dxbc)
{
    uint8_t isgn[T_DXBC_MAX], isg1[T_DXBC_MAX], c[T_DXBC_MAX], m[T_DXBC_MAX], filler[40];
    char why[RT_DXBC_WHY];
    RtDxbc d;
    RtDxbcElement e;
    uint32_t tag, len, n;
    const uint8_t *data;

    T_CASE("the checksum agrees with another implementation's on both sides of the 56-byte boundary");
    static const struct { uint32_t size; const char *hex; } known[] = {
        { 20,   "140d60f6b775e2ba4e4abed401b2e9a1" },   /* nothing hashed */
        { 21,   "43bb27ddf151b13cf69e5f57797fd4b5" },   /* 1 byte left */
        { 75,   "dd57711e3e12fe7e84c2f47ede426641" },   /* 55: the last length that shares the final block */
        { 76,   "d571cc66536cb7b29e8780ffbe41a319" },   /* 56: the first that takes a block of its own */
        { 83,   "0ad1118e2eaa276bd451408f2991d166" },   /* 63 */
        { 84,   "c1f54b1b564c9ad315e16f47b5ffcb18" },   /* one whole block, 0 left */
        { 85,   "e00e724f53dccf88b06a0739b47975ad" },   /* one whole block and 1 */
        { 148,  "92f40506414668174c7615900f9394b2" },   /* two whole blocks */
        { 4432, "7aa121773cf84e1312fba019f5a80980" },   /* 68 whole blocks and 60 */
    };
    static uint8_t pattern[4432];
    for (unsigned i = 0; i < sizeof pattern; i++) pattern[i] = (uint8_t)(i * 7 + 3);
    for (unsigned k = 0; k < sizeof known / sizeof known[0]; k++) {
        uint8_t sum[16];
        char hex[33];
        rt_dxbc_checksum(pattern, known[k].size, sum);
        for (int i = 0; i < 16; i++) { hex[2 * i] = "0123456789abcdef"[sum[i] >> 4]; hex[2 * i + 1] = "0123456789abcdef"[sum[i] & 15]; }
        hex[32] = 0;
        if (strcmp(hex, known[k].hex)) rt_test_fail(__FILE__, __LINE__, "%u bytes: checksum %s, expected %s", known[k].size, hex, known[k].hex);
        T_TRUE(!strcmp(hex, known[k].hex));
    }

    /* Three chunks: filler standing for a shader's other chunks before and after the signature. */
    for (unsigned i = 0; i < sizeof filler; i++) filler[i] = (uint8_t)(0xc0 + i);
    uint32_t isgn_len = t_dxbc_signature(isgn, false, sig, NSIG);
    uint32_t isg1_len = t_dxbc_signature(isg1, true, sig, NSIG);
    /* By hand: 8 + 4 * 24 = 104, names 9 + 9 + 12 + 6 = 36: 140.  ISG1: 8 + 4 * 32 + 36 = 172. */
    T_EQ(isgn_len, 140);
    T_EQ(isg1_len, 172);
    const TDxbcChunk chunks[] = { { "RDEF", filler, 40 }, { "ISGN", isgn, isgn_len }, { "SHDR", filler, 13 } };
    uint32_t size = t_dxbc_container(c, chunks, 3);

    T_CASE("a well-formed container opens: its chunks, in order, with their tags, lengths and data");
    T_EQ(size, 32 + 12 + (8 + 40) + (8 + 140) + (8 + 13));           /* 261: a size that is no multiple of 4 */
    memset(why, 0, sizeof why);
    T_REQUIRE(rt_dxbc_open(c, size, &d, why));
    T_EQ(d.nchunks, 3);
    T_EQ(d.size, 261);
    T_TRUE(d.bytes == c);
    rt_dxbc_chunk(&d, 0, &tag, &data, &len);
    T_EQ(tag, RT_DXBC_TAG('R', 'D', 'E', 'F'));  T_EQ(len, 40);   T_TRUE(data == c + 44 + 8);
    rt_dxbc_chunk(&d, 1, &tag, &data, &len);
    T_EQ(tag, RT_DXBC_ISGN);                     T_EQ(len, 140);  T_TRUE(data == c + 92 + 8);
    T_TRUE(!memcmp(data, isgn, 140));
    rt_dxbc_chunk(&d, 2, &tag, &data, &len);
    T_EQ(tag, RT_DXBC_TAG('S', 'H', 'D', 'R'));  T_EQ(len, 13);   T_TRUE(data == c + 240 + 8);
    char text[5];
    rt_dxbc_tag_text(RT_DXBC_ISG1, text);
    T_TRUE(!strcmp(text, "ISG1"));
    rt_dxbc_tag_text(0x41000a42, text);
    T_TRUE(!strcmp(text, "B??A"));

    T_CASE("an ISGN signature: every field of every element as the chunk has it");
    n = 99;
    T_REQUIRE(rt_dxbc_signature_count(RT_DXBC_ISGN, isgn, isgn_len, &n, why));
    T_EQ(n, NSIG);
    uint32_t at = 104;                                               /* the first name: after the four elements */
    for (uint32_t i = 0; i < NSIG; i++) {
        memset(&e, 0xee, sizeof e);
        T_REQUIRE(rt_dxbc_signature_element(RT_DXBC_ISGN, isgn, isgn_len, i, &e, why));
        T_TRUE(e.name == (const char *)isgn + at);
        T_TRUE(!strcmp(e.name, sig[i].name));
        T_EQ(e.name_length, strlen(sig[i].name));
        T_EQ(e.semantic_index, sig[i].index);
        T_EQ(e.system_value, sig[i].system_value);
        T_EQ(e.component_type, sig[i].component_type);
        T_EQ(e.reg, sig[i].reg);
        T_EQ(e.mask, sig[i].mask);
        T_EQ(e.rw_mask, sig[i].rw_mask);
        T_EQ(e.stream, 0);                                           /* ISGN has neither */
        T_EQ(e.min_precision, 0);
        at += (uint32_t)strlen(sig[i].name) + 1;
    }

    T_CASE("an ISG1 signature: the same, with the stream before and the minimum precision after");
    T_REQUIRE(rt_dxbc_signature_count(RT_DXBC_ISG1, isg1, isg1_len, &n, why));
    T_EQ(n, NSIG);
    for (uint32_t i = 0; i < NSIG; i++) {
        memset(&e, 0xee, sizeof e);
        T_REQUIRE(rt_dxbc_signature_element(RT_DXBC_ISG1, isg1, isg1_len, i, &e, why));
        T_TRUE(!strcmp(e.name, sig[i].name));
        T_EQ(e.semantic_index, sig[i].index);
        T_EQ(e.system_value, sig[i].system_value);
        T_EQ(e.component_type, sig[i].component_type);
        T_EQ(e.reg, sig[i].reg);
        T_EQ(e.mask, sig[i].mask);
        T_EQ(e.rw_mask, sig[i].rw_mask);
        T_EQ(e.stream, sig[i].stream);
        T_EQ(e.min_precision, sig[i].min_precision);
    }
    /* An element array that does not begin at 8: the offset field is followed, as Microsoft's parser does. */
    memcpy(m, isgn, 8);
    memset(m + 8, 0x5a, 4);
    memcpy(m + 12, isgn + 8, isgn_len - 8);
    t_dxbc_put32(m + 4, 12);
    for (uint32_t i = 0; i < NSIG; i++) t_dxbc_put32(m + 12 + 24 * i, 104 + 4 + (uint32_t)(i ? i == 1 ? 9 : i == 2 ? 18 : 30 : 0));
    T_REQUIRE(rt_dxbc_signature_count(RT_DXBC_ISGN, m, isgn_len + 4, &n, why));
    T_EQ(n, NSIG);
    T_REQUIRE(rt_dxbc_signature_element(RT_DXBC_ISGN, m, isgn_len + 4, 2, &e, why));
    T_TRUE(!strcmp(e.name, "SV_VertexID"));
    T_EQ(e.system_value, 6);
    /* A signature with no element is a signature. */
    uint8_t none[8];
    T_EQ(t_dxbc_signature(none, false, sig, 0), 8);
    n = 99;
    T_TRUE(rt_dxbc_signature_count(RT_DXBC_ISGN, none, 8, &n, why));
    T_EQ(n, 0);

    T_CASE("a header that is refused: too short, not DXBC, another version, another size, a wrong checksum");
    T_TRUE(refused(c, 0, "0 bytes: shorter than the 32-byte header"));
    T_TRUE(refused(c, 31, "31 bytes: shorter than the 32-byte header"));
    T_TRUE(refused(c, UINT64_C(0x20000000), "2^29 or more"));
    memcpy(m, c, size); m[3] = 'X';
    T_TRUE(refused(m, size, "it does not begin with DXBC (its first four bytes are 44 58 42 58)"));
    memcpy(m, c, size); m[20] = 2; t_dxbc_stamp(m, size);
    T_TRUE(refused(m, size, "container version 0x2, not 1"));
    T_TRUE(refused(c, size - 1, "the header declares 261 bytes and the caller passed 260"));
    T_TRUE(refused(c, size + 3, "the header declares 261 bytes and the caller passed 264"));
    T_TRUE(refused(c, 32, "the header declares 261 bytes and the caller passed 32"));
    memcpy(m, c, size); m[size - 1] ^= 1;                            /* one bit of the last chunk's data */
    T_TRUE(refused(m, size, "these are not the bytes the compiler wrote"));
    memcpy(m, c, size); m[4] ^= 0x80;                                /* one bit of the stored checksum */
    T_TRUE(refused(m, size, "these are not the bytes the compiler wrote"));
    memcpy(m, c, size); m[30] ^= 1;                                  /* a header byte that is hashed: the chunk count */
    T_TRUE(refused(m, size, "these are not the bytes the compiler wrote"));

    T_CASE("a chunk table that is refused: a count that does not fit, an offset inside the table, an offset past the end, data past the end");
    memcpy(m, c, size); t_dxbc_put32(m + 28, 58); t_dxbc_stamp(m, size);      /* (261 - 32) / 4 = 57 offsets fit */
    T_TRUE(refused(m, size, "58 chunks: their offset table does not fit in the container's 261 bytes"));
    memcpy(m, c, size); t_dxbc_put32(m + 28, 0xffffffff); t_dxbc_stamp(m, size);
    T_TRUE(refused(m, size, "4294967295 chunks: their offset table does not fit"));
    memcpy(m, c, size); t_dxbc_put32(m + 36, 40); t_dxbc_stamp(m, size);      /* chunk 1 inside the table (32 + 12 = 44) */
    T_TRUE(refused(m, size, "chunk 1 at offset 40 lies inside the header and offset table (44 bytes)"));
    memcpy(m, c, size); t_dxbc_put32(m + 36, 0); t_dxbc_stamp(m, size);
    T_TRUE(refused(m, size, "chunk 1 at offset 0 lies inside the header and offset table"));
    memcpy(m, c, size); t_dxbc_put32(m + 40, 261); t_dxbc_stamp(m, size);     /* chunk 2 at the very end */
    T_TRUE(refused(m, size, "chunk 2 at offset 261: its 8-byte header runs past the container's 261 bytes"));
    memcpy(m, c, size); t_dxbc_put32(m + 40, 254); t_dxbc_stamp(m, size);     /* 7 bytes left for an 8-byte header */
    T_TRUE(refused(m, size, "chunk 2 at offset 254: its 8-byte header runs past"));
    memcpy(m, c, size); t_dxbc_put32(m + 32, 0xfffffff0); t_dxbc_stamp(m, size);
    T_TRUE(refused(m, size, "chunk 0 at offset 4294967280: its 8-byte header runs past"));
    memcpy(m, c, size); t_dxbc_put32(m + 240 + 4, 14); t_dxbc_stamp(m, size); /* chunk 2 has 13 bytes of room */
    T_TRUE(refused(m, size, "chunk 2 (SHDR) at offset 240: its data of 14 bytes runs past the container's 261 bytes"));
    memcpy(m, c, size); t_dxbc_put32(m + 92 + 4, 0xffffffff); t_dxbc_stamp(m, size);
    T_TRUE(refused(m, size, "chunk 1 (ISGN) at offset 92: its data of 4294967295 bytes runs past"));
    /* A chunk that ends exactly at the end, and one of no bytes there, are inside. */
    memcpy(m, c, size); t_dxbc_put32(m + 40, 253); t_dxbc_put32(m + 253 + 4, 0); t_dxbc_stamp(m, size);
    T_TRUE(rt_dxbc_open(m, size, &d, why));

    T_CASE("a signature that is refused: not a signature, no header, an offset outside, a count that does not fit, a name outside, a name with no end, a component type");
    T_TRUE(sig_refused(RT_DXBC_TAG('O', 'S', 'G', 'N'), isgn, isgn_len, "OSGN is not an input-signature chunk"));
    T_TRUE(sig_refused(RT_DXBC_ISGN, isgn, 7, "the ISGN chunk has 7 bytes: shorter than its 8-byte header"));
    memcpy(m, isgn, isgn_len); t_dxbc_put32(m + 4, 4);
    T_TRUE(sig_refused(RT_DXBC_ISGN, m, isgn_len, "the ISGN chunk of 140 bytes puts its element array at offset 4"));
    memcpy(m, isgn, isgn_len); t_dxbc_put32(m + 4, 141);
    T_TRUE(sig_refused(RT_DXBC_ISGN, m, isgn_len, "puts its element array at offset 141"));
    memcpy(m, isgn, isgn_len); t_dxbc_put32(m, 6);                           /* (140 - 8) / 24 = 5 fit */
    T_TRUE(sig_refused(RT_DXBC_ISGN, m, isgn_len, "the ISGN chunk's 6 elements of 24 bytes at offset 8 do not fit in its 140 bytes"));
    memcpy(m, isgn, isgn_len); t_dxbc_put32(m, 0x0aaaaaab);                  /* times 24 wraps to 8 in 32 bits */
    T_TRUE(sig_refused(RT_DXBC_ISGN, m, isgn_len, "178956971 elements of 24 bytes at offset 8 do not fit"));
    T_TRUE(sig_refused(RT_DXBC_ISG1, isg1, 8 + 4 * 32 - 1, "the ISG1 chunk's 4 elements of 32 bytes at offset 8 do not fit in its 135 bytes"));
    memcpy(m, isgn, isgn_len); t_dxbc_put32(m + 8 + 24, 140);                /* element 1's name: one past the end */
    T_TRUE(sig_refused(RT_DXBC_ISGN, m, isgn_len, "ISGN element 1: its name at offset 140 lies outside the chunk's 140 bytes"));
    memcpy(m, isgn, isgn_len); t_dxbc_put32(m + 8 + 24, 0xfffffff8);
    T_TRUE(sig_refused(RT_DXBC_ISGN, m, isgn_len, "ISGN element 1: its name at offset 4294967288 lies outside"));
    /* The last name with its NUL cut off: "COLOR" begins at 134 and its NUL is the chunk's last byte, 139. */
    T_TRUE(sig_refused(RT_DXBC_ISGN, isgn, 139, "ISGN element 3: its name at offset 134 does not end inside the chunk's 139 bytes"));
    memcpy(m, isgn, isgn_len); t_dxbc_put32(m + 8 + 2 * 24 + 12, 4);
    T_TRUE(sig_refused(RT_DXBC_ISGN, m, isgn_len, "ISGN element 2: component type 4 is none of D3D_REGISTER_COMPONENT_TYPE's four"));
}
