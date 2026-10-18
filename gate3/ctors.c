/* Counting static constructors: see ctors.h.  Like gate2/peek.c, this file reads a DLL only
 * through the loader's own reader (runtime/core/loader.h: identity checked). */
#include <stdlib.h>
#include <string.h>

#include "../runtime/core/loader.h"
#include "ctors.h"

Gate3Shared *gate3_shared;

/* ---- finding the tables ------------------------------------------------------------------------- */

static const RtLdrSection *section_of(const RtImage *img, uint32_t rva) {
    for (unsigned i = 0; i < img->nsections; i++) {
        const RtLdrSection *s = &img->sections[i];
        if (rva >= s->rva && rva - s->rva < s->vsize) return s;
    }
    return NULL;
}

/* `lea r64, [rip+disp32]` at p (rva `at`): REX.W with or without REX.R, 8D, modrm 00 reg 101. */
static bool lea_rip(const uint8_t *p, uint32_t at, uint32_t *target) {
    if ((p[0] != 0x48 && p[0] != 0x4C) || p[1] != 0x8D || (p[2] & 0xC7) != 0x05) return false;
    int32_t d;
    memcpy(&d, p + 3, 4);
    *target = at + 7 + (uint32_t)d;
    return true;
}

/* Is [lo, hi) a table of initializers?  Fills t and returns true if so. */
static bool table_ok(const RtImage *img, const RtModule *m, uint32_t lo, uint32_t hi, Gate3InitTable *t) {
    if (lo >= hi || (hi - lo) % 8 || hi - lo > 0x80000) return false;
    const RtLdrSection *s = section_of(img, lo);
    if (!s || (s->chars & IMAGE_SCN_MEM_EXECUTE) || section_of(img, hi - 8) != s) return false;
    const uint8_t *slots = rt_ldr_rva(img, lo, hi - lo);
    if (!slots) return false;
    unsigned nonnull = 0;
    for (uint32_t i = 0; i < (hi - lo) / 8; i++) {
        uint64_t v;
        memcpy(&v, slots + 8 * i, 8);
        if (!v) continue;
        if (i == 0) return false;                               /* the first slot is the null that marks the start */
        if (v - img->preferred_base >= img->size) return false;
        if (!gate2_has_lifted_entry(m, (uint32_t)(v - img->preferred_base))
            && !rt_lifted_skip(m->def->code, (uint32_t)(v - img->preferred_base))) return false;
        nonnull++;
    }
    if (!nonnull) return false;
    t->lo = lo;
    t->hi = hi;
    t->nonnull = nonnull;
    return true;
}

void gate3_ctor_scan(RtModule *m, Gate3Ctors *out) {
    memset(out, 0, sizeof *out);
    RtLdrModule *lm = rt_ldr_module(m);
    if (lm->state == RT_LDR_ABSENT) return;
    RtImage *img = malloc(sizeof *img);
    if (!img) rt_fatal("out of memory");
    if (!rt_ldr_image_scan(lm, img)) rt_fatal("%s: the DLL file was there at start-up and is gone", m->name);
    for (unsigned si = 0; si < img->nsections; si++) {
        const RtLdrSection *s = &img->sections[si];
        if (!(s->chars & IMAGE_SCN_MEM_EXECUTE) || s->vsize < 14) continue;
        const uint8_t *code = rt_ldr_rva(img, s->rva, s->vsize);
        if (!code) continue;
        for (uint32_t off = 0; off + 14 <= s->vsize; off++) {
            uint32_t a, b;
            Gate3InitTable t;
            if (!lea_rip(code + off, s->rva + off, &a) || !lea_rip(code + off + 7, s->rva + off + 7, &b)) continue;
            if (!table_ok(img, m, a < b ? a : b, a < b ? b : a, &t)) continue;
            bool have = false;
            for (unsigned k = 0; k < out->ntables; k++) have |= out->tables[k].lo == t.lo && out->tables[k].hi == t.hi;
            if (have || out->ntables == GATE3_MAX_TABLES) continue;
            t.site = s->rva + off;
            out->tables[out->ntables++] = t;
        }
    }
    for (unsigned k = 0; k < out->ntables; k++) out->nfn += out->tables[k].nonnull;
    out->fn_rva = calloc(out->nfn ? out->nfn : 1, sizeof *out->fn_rva);
    out->fn_table = calloc(out->nfn ? out->nfn : 1, 1);
    if (!out->fn_rva || !out->fn_table) rt_fatal("out of memory");
    unsigned n = 0;
    for (unsigned k = 0; k < out->ntables; k++) {
        const uint8_t *slots = rt_ldr_rva(img, out->tables[k].lo, out->tables[k].hi - out->tables[k].lo);
        for (uint32_t i = 0; i < (out->tables[k].hi - out->tables[k].lo) / 8; i++) {
            uint64_t v;
            memcpy(&v, slots + 8 * i, 8);
            if (!v) continue;
            out->fn_rva[n] = (uint32_t)(v - img->preferred_base);
            out->fn_table[n++] = (uint8_t)k;
        }
    }
    rt_ldr_image_release(img);
    free(img);
}

/* ---- the counting functions ------------------------------------------------------------------------ */

static LiftedFn real_fn[GATE3_CTOR_POOL];
static uint8_t  wrap_module[GATE3_CTOR_POOL], wrap_table[GATE3_CTOR_POOL];

static inline void note(unsigned k) {
    if (!gate3_shared->seen[k]) {
        gate3_shared->seen[k] = 1;
        gate3_shared->hits[wrap_module[k]][wrap_table[k]]++;
    }
}

/* One host function per counted entry, made by macro as the runtime's trap stubs are
 * (runtime/core/trap.c).  The lifted function is entered by a tail call: no host frame stays. */
#define W0(x)  static void ctor_##x(Ctx *ctx) { note(0x##x); __attribute__((musttail)) return real_fn[0x##x](ctx); }
#define W1(x)  W0(x##0) W0(x##1) W0(x##2) W0(x##3) W0(x##4) W0(x##5) W0(x##6) W0(x##7) \
               W0(x##8) W0(x##9) W0(x##A) W0(x##B) W0(x##C) W0(x##D) W0(x##E) W0(x##F)
#define W2(x)  W1(x##0) W1(x##1) W1(x##2) W1(x##3) W1(x##4) W1(x##5) W1(x##6) W1(x##7) \
               W1(x##8) W1(x##9) W1(x##A) W1(x##B) W1(x##C) W1(x##D) W1(x##E) W1(x##F)
#define W3(x)  W2(x##0) W2(x##1) W2(x##2) W2(x##3) W2(x##4) W2(x##5) W2(x##6) W2(x##7) \
               W2(x##8) W2(x##9) W2(x##A) W2(x##B) W2(x##C) W2(x##D) W2(x##E) W2(x##F)
W3(0) W3(1) W3(2) W3(3) W3(4) W3(5) W3(6) W3(7)
#undef W0
#define W0(x)  ctor_##x,
static const LiftedFn pool[GATE3_CTOR_POOL] = { W3(0) W3(1) W3(2) W3(3) W3(4) W3(5) W3(6) W3(7) };
#undef W0
#undef W1
#undef W2
#undef W3

const RtModuleDef *gate3_ctor_wrap(const RtModuleDef *defs, unsigned n, const Gate3Ctors *ctors, unsigned nctors,
                                   unsigned *uncounted) {
    RtModuleDef *out = calloc(n + 1, sizeof *out);                  /* the terminator row is copied too */
    if (!out) rt_fatal("out of memory");
    memcpy(out, defs, (n + 1) * sizeof *out);
    unsigned used = 0, li = 0;
    *uncounted = 0;
    for (unsigned i = 0; i < n; i++) {
        if (out[i].kind != RT_MOD_LIFTED) continue;
        if (li >= nctors) break;
        const Gate3Ctors *c = &ctors[li];
        const RtLiftedCode *code = out[i].code;
        if (c->nfn) {
            size_t rows = *code->count;
            LiftedEntry *table = malloc((rows + 1) * sizeof *table);
            RtLiftedCode *copy = malloc(sizeof *copy);
            if (!table || !copy) rt_fatal("out of memory");
            memcpy(table, code->table, (rows + 1) * sizeof *table);
            for (unsigned k = 0; k < c->nfn; k++) {
                size_t lo = 0, hi = rows;
                while (lo < hi) {
                    size_t mid = lo + (hi - lo) / 2;
                    if (table[mid].rva < c->fn_rva[k]) lo = mid + 1; else hi = mid;
                }
                if (lo == rows || table[lo].rva != c->fn_rva[k]) rt_fatal("%s: initializer +%#x has no row", out[i].file, c->fn_rva[k]);
                if (table[lo].fn != code->table[lo].fn) continue;   /* the same function twice: counted once */
                if (used == GATE3_CTOR_POOL) { (*uncounted)++; continue; }
                real_fn[used] = table[lo].fn;
                wrap_module[used] = (uint8_t)li;
                wrap_table[used] = c->fn_table[k];
                table[lo].fn = pool[used++];
            }
            *copy = *code;
            copy->table = table;
            out[i].code = copy;
        }
        li++;
    }
    return out;
}
