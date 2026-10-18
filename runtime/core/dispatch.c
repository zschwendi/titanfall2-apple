/* The dispatch table (docs/RUNTIME.md section 2).  Interface: rt/dispatch.h. */
#include <sys/mman.h>

#include "core.h"

#define DISP_ENTRIES (UINT64_C(1) << RT_DISP_BITS)
#define DISP_BYTES   (DISP_ENTRIES * sizeof(RtDispEntry))

const RtDispEntry *g_disp;
unsigned           g_disp_shift = 64 - RT_DISP_BITS;
uint64_t           g_disp_mask = DISP_ENTRIES - 1;

static RtDispEntry *table;          /* the same memory as g_disp, writable until sealed */
static size_t       count;
static bool         sealed;

_Static_assert(sizeof(RtDispEntry) == 16, "RtDispEntry");

/* What an empty slot holds.  rt_lookup compares the key first, and the key of an empty slot
 * is 0, so a lookup of guest address 0 (a call through a null function pointer) "hits" the
 * first empty slot it probes and returns that slot's function.  Making that function this
 * one turns a null call into a dispatch-miss report at no cost on the hot path. */
static void dispatch_null(Ctx *ctx) {
    RT_THREAD(ctx)->miss_target = 0;
    rt_dispatch_miss(ctx);
}

void rt_disp_init(void) {
    if (table) rt_fatal("rt_disp_init called twice");
    void *p = mmap(NULL, DISP_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) rt_fatal("cannot allocate the dispatch table (%llu bytes)", (unsigned long long)DISP_BYTES);
    table = p;
    g_disp = p;
    for (uint64_t i = 0; i < DISP_ENTRIES; i++) table[i].fn = dispatch_null;
}

void rt_disp_add(uint64_t guest, LiftedFn fn) {
    char where[160];
    if (!table) rt_fatal("rt_disp_add before rt_disp_init");
    if (sealed)
        rt_fatal("rt_disp_add(%s) after the dispatch table was sealed: every guest code address must be "
                 "registered during rt_init", rt_module_describe(guest, where, sizeof where));
    if (guest == 0 || fn == NULL) rt_fatal("rt_disp_add: null guest address or function");
    if (count >= DISP_ENTRIES / 2) rt_fatal("the dispatch table is half full (%zu keys); raise RT_DISP_BITS", count);
    uint64_t i = (guest * UINT64_C(0x9E3779B97F4A7C15)) >> g_disp_shift;
    for (;; i = (i + 1) & g_disp_mask) {
        RtDispEntry *e = &table[i];
        if (e->guest == guest) {
            if (e->fn == fn) return;
            rt_fatal("guest address %s is registered with two different host functions",
                     rt_module_describe(guest, where, sizeof where));
        }
        if (e->guest == 0) { e->guest = guest; e->fn = fn; count++; return; }
    }
}

void rt_disp_add_table(uint64_t base, const LiftedEntry *entries, size_t n) {
    for (size_t i = 0; i < n; i++) rt_disp_add(base + entries[i].rva, entries[i].fn);
}

void rt_disp_seal(void) {
    if (!table) rt_fatal("rt_disp_seal before rt_disp_init");
    if (mprotect(table, DISP_BYTES, PROT_READ) != 0) rt_fatal("cannot write-protect the dispatch table");
    sealed = true;
}

bool   rt_disp_sealed(void) { return sealed; }
size_t rt_disp_count(void)  { return count; }

LiftedFn rt_disp_find(uint64_t guest) {
    if (!table || guest == 0) return NULL;
    uint64_t i = (guest * UINT64_C(0x9E3779B97F4A7C15)) >> g_disp_shift;
    for (;; i = (i + 1) & g_disp_mask) {
        if (g_disp[i].guest == guest) return g_disp[i].fn;
        if (g_disp[i].guest == 0) return NULL;
    }
}

/* For native callers.  Lifted code calls rt_dispatch_fn (TF_LOOKUP in harness/runtime.h). */
void rt_dispatch(Ctx *ctx, uint64_t guest_target) {
    rt_lookup(ctx, guest_target)(ctx);
}

LiftedFn rt_dispatch_fn(Ctx *ctx, uint64_t guest_target) {
    return rt_lookup(ctx, guest_target);
}

/* harness/runtime.h: the lock for locked operands that straddle a 16-byte boundary.  Defined
 * once by whoever links the lifted code, which in the real program is the runtime. */
uint8_t tf_straddle_lock;
