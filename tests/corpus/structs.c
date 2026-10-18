/* Structs by value and by pointer, bitfields, linked data. */
#include "corpus.h"

typedef struct { i32 x, y; } Pt;                       /* 8 bytes: returned in rax */
typedef struct { i64 a, b, c; } Big;                   /* 24 bytes: returned through a hidden pointer */
typedef struct { u8 tag; u16 len; u32 flags; u64 id; } Rec;
typedef struct { u32 lo : 5, mid : 11, hi : 16; } Bits;
typedef struct Node { struct Node *next; i32 val; } Node;

EXPORT Pt pt_add(Pt a, Pt b) { Pt r = { a.x + b.x, a.y - b.y }; return r; }
EXPORT Big big_make(i64 a, i64 b) { Big r = { a + b, a - b, a * b }; return r; }
EXPORT i64 big_sum(Big v) { return v.a + v.b * 2 + v.c * 3; }
EXPORT Big big_chain(i64 s) { Big t = big_make(s, 3); t.a += big_sum(t); return t; }
EXPORT u64 rec_pack(const Rec *r) { return ((u64)r->tag << 56) ^ ((u64)r->len << 32) ^ r->flags ^ (r->id * 0x100000001B3ULL); }
EXPORT void rec_fill(Rec *r, u32 seed) { r->tag = (u8)seed; r->len = (u16)(seed >> 8); r->flags = seed * 2654435761u; r->id = (u64)seed << 20 | seed; }
EXPORT u32 bits_get(Bits b) { return b.lo + b.mid * 2 + b.hi * 3; }
EXPORT Bits bits_set(u32 v) { Bits b; b.lo = v; b.mid = v >> 5; b.hi = v >> 16; return b; }
EXPORT i64 list_sum(const Node *n) { i64 s = 0; for (u32 i = 0; n && i < 16; i++, n = n->next) s += n->val; return s; }
EXPORT u32 list_build(Node *pool, u32 n, i32 start) {
    n = (n & 7) + 1;
    for (u32 i = 0; i < n; i++) { pool[i].val = start + (i32)i * 3; pool[i].next = i + 1 < n ? &pool[i + 1] : 0; }
    return (u32)list_sum(pool);
}
EXPORT void matrix_mul(i32 out[3][3], const i32 a[3][3], const i32 b[3][3]) {
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { i32 s = 0; for (int k = 0; k < 3; k++) s += a[i][k] * b[k][j]; out[i][j] = s; }
}
static const u16 kTable[16] = { 3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3 };
static i32 gCounter = 100;
static const char *const kNames[4] = { "alpha", "beta", "gamma", "delta" };
EXPORT u32 table_lookup(u32 i) { return kTable[i & 15] * 7u + kTable[(i >> 4) & 15]; }
EXPORT i32 counter_bump(i32 by) { gCounter += by; return gCounter; }
EXPORT char name_char(u32 which, u32 at) { const char *s = kNames[which & 3]; return s[at % 4]; }
