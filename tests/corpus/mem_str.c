/* Memory and string routines, hashing, sorting. Also provides the mem* functions the
   compiler may call on its own. */
#include "corpus.h"

/* The Microsoft ABI references this marker from any object that uses floating point. */
int _fltused = 1;

void *memcpy(void *d, const void *s, usize n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, usize n) { u8 *a = d; while (n--) *a++ = (u8)c; return d; }
void *memmove(void *d, const void *s, usize n) {
    u8 *a = d; const u8 *b = s;
    if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
EXPORT usize str_len(const char *s) { const char *p = s; while (*p && (usize)(p - s) < 4096) p++; return (usize)(p - s); }
EXPORT i32 str_cmp(const char *a, const char *b, usize max) {
    for (usize i = 0; i < max; i++) { u8 x = (u8)a[i], y = (u8)b[i]; if (x != y) return x < y ? -1 : 1; if (!x) break; }
    return 0;
}
EXPORT i32 mem_cmp(const void *a, const void *b, usize n) {
    const u8 *x = a, *y = b; for (usize i = 0; i < n; i++) if (x[i] != y[i]) return (i32)x[i] - (i32)y[i]; return 0;
}
EXPORT void copy_words(u32 *d, const u32 *s, u32 n) { n &= 63; for (u32 i = 0; i < n; i++) d[i] = s[i]; }
EXPORT void fill_pattern(u8 *d, u32 n, u8 seed) { n &= 255; for (u32 i = 0; i < n; i++) { d[i] = seed; seed = (u8)(seed * 5 + 1); } }
EXPORT void overlap_move(u8 *buf, u32 from, u32 to, u32 n) { memmove(buf + (to & 63), buf + (from & 63), n & 63); }
EXPORT u32 fnv1a(const u8 *p, u32 n) { u32 h = 2166136261u; n &= 255; for (u32 i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
EXPORT u32 crc32(const u8 *p, u32 n) {
    u32 c = 0xFFFFFFFFu; n &= 127;
    for (u32 i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}
EXPORT u32 adler(const u8 *p, u32 n) { u32 a = 1, b = 0; n &= 255; for (u32 i = 0; i < n; i++) { a = (a + p[i]) % 65521u; b = (b + a) % 65521u; } return (b << 16) | a; }
EXPORT void insertion_sort(i32 *v, u32 n) {
    n &= 31;
    for (u32 i = 1; i < n; i++) { i32 x = v[i]; u32 j = i; while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; j--; } v[j] = x; }
}
typedef i32 (*cmp_fn)(const void *, const void *);
static i32 cmp_i32(const void *a, const void *b) { i32 x = *(const i32 *)a, y = *(const i32 *)b; return (x > y) - (x < y); }
static i32 cmp_u16_desc(const void *a, const void *b) { u16 x = *(const u16 *)a, y = *(const u16 *)b; return (y > x) - (y < x); }
static void swap_bytes(u8 *a, u8 *b, usize sz) { while (sz--) { u8 t = *a; *a++ = *b; *b++ = t; } }
NOINLINE static void sort_generic(void *base, u32 n, usize sz, cmp_fn cmp) {
    u8 *v = base;
    for (u32 i = 1; i < n; i++) for (u32 j = i; j > 0 && cmp(v + (j - 1) * sz, v + j * sz) > 0; j--) swap_bytes(v + (j - 1) * sz, v + j * sz, sz);
}
EXPORT void sort_ints(i32 *v, u32 n) { sort_generic(v, n & 15, sizeof *v, cmp_i32); }
EXPORT void sort_shorts_desc(u16 *v, u32 n) { sort_generic(v, n & 15, sizeof *v, cmp_u16_desc); }
EXPORT i32 bsearch_i32(const i32 *v, u32 n, i32 key) {
    u32 lo = 0, hi = n & 31;
    while (lo < hi) { u32 mid = (lo + hi) / 2; if (v[mid] < key) lo = mid + 1; else hi = mid; }
    return (i32)lo;
}
EXPORT u32 utoa10(u32 v, char *out) { char tmp[12]; u32 n = 0; do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v); for (u32 i = 0; i < n; i++) out[i] = tmp[n - 1 - i]; out[n] = 0; return n; }
EXPORT u32 hex_parse(const char *s) {
    u32 v = 0;
    for (u32 i = 0; i < 8; i++) { char c = s[i]; u32 d; if (c >= '0' && c <= '9') d = (u32)(c - '0'); else if (c >= 'a' && c <= 'f') d = (u32)(c - 'a' + 10); else if (c >= 'A' && c <= 'F') d = (u32)(c - 'A' + 10); else break; v = (v << 4) | d; }
    return v;
}
