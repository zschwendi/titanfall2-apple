/* The loader: static TLS and the loader's per-thread state (docs/RUNTIME.md section 1.6
 * step 4, section 6.2).  Specification: loader.h under "static TLS and threads" (who does
 * what, what a block is, the list of recorded threads), and rt_loader_in_entry in
 * rt/module.h.
 *
 * State, all guarded by the loader lock: the table of modules that have a TLS directory,
 * indexed by static TLS index (made at start-up, `live` from rt_ldr_tls_load on); the list
 * of recorded threads.  A thread's record hangs off RtThread.sub[RT_SUB_LOADER].
 *
 * DECISIONS.
 *   - Which blocks a thread has is read from its own array in guest memory (a slot that is
 *     not 0 is a block to free); there is no host-side copy.
 *   - The TLS directory is checked at start-up on the scanned image (addresses relative to
 *     the file's ImageBase) and again at load on the mapped one (relative to the module's
 *     base, after relocation), by the same routine.  An image without relocations that is
 *     mapped away from its ImageBase therefore fails at load with "outside the image".
 *   - A block of size 0 (no template, no zero fill) is allocated as 16 bytes, so that every
 *     live module has a non-zero slot in every recorded thread.
 *   - Blocks are 16-byte aligned (what rt_galloc gives).  A directory whose Characteristics
 *     ask for more gets a `loader` warning, once per module, and 16 bytes all the same:
 *     whether the Windows 7 loader honours that field is not verified.
 *
 * rt_loader_in_entry AND SIGNAL HANDLERS.  The fatal report calls it from a signal handler
 * on the thread the report is about, with no lock.  What it reads, and why that is safe:
 *   1. t->sub[RT_SUB_LOADER].  Only thread t writes it (rt_ldr_tls_thread stores the pointer
 *      after the record is complete, rt_ldr_tls_thread_end clears it before the record is
 *      freed), each time with one store.  A handler on t interrupts t between two
 *      instructions, so it sees NULL or a complete record, and the record cannot be freed
 *      under it, because the only thread that frees it is the one the handler is running on.
 *   2. RtLdrThread.entry, ONE word that holds the module and the reason together (the
 *      RtModule's address with the reason, 0 to 3, in its two low bits; 0: in none).  Only
 *      thread t writes it, with one store, so the handler never sees the module of one call
 *      with the reason of another, which two separate words would allow.  (rt/module.h says
 *      "two words"; one is what makes the pair indivisible.)
 *   3. The RtModule the word names: the registry is never freed.
 * For a thread other than the caller's the same read is safe only under the loader lock.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader.h"
#include "rt/heap.h"
#include "rt/win/teb.h"

_Static_assert(_Alignof(RtModule) >= 4, "RtLdrThread.entry keeps the reason in the two low bits of an RtModule address");

typedef struct {
    RtLdrModule *lm;
    bool         live;              /* rt_ldr_tls_load ran: every recorded thread has a block */
    uint32_t     tpl_rva, tpl_size; /* the template in the mapped image; valid when live */
    uint32_t     zero_fill;
} TlsModule;

static TlsModule   *tls_mods;       /* [0, next_index) */
static int32_t      next_index;
static RtLdrThread *threads;
static unsigned     nthreads;

/* ---- the directory ------------------------------------------------------------------------- */

typedef struct { uint32_t tpl_rva, tpl_size, zero_fill, index_rva, callbacks_rva; } TlsDir;   /* callbacks_rva 0: none */

/* Read and check img's TLS directory.  Its four addresses are virtual addresses: in the
 * file they assume ImageBase, in the mapped image the relocations have moved them to the
 * module's base. */
static void read_dir(const RtLdrModule *lm, const RtImage *img, TlsDir *out) {
    const char *name = lm->m->name;
    const IMAGE_DATA_DIRECTORY dd = img->dir[IMAGE_DIRECTORY_ENTRY_TLS];
    IMAGE_TLS_DIRECTORY64 d;
    if (!dd.Size) rt_fatal("%s has no TLS directory", name);
    const void *p = rt_ldr_rva(img, dd.VirtualAddress, sizeof d);
    if (!p || dd.Size < sizeof d) rt_fatal("%s: its TLS directory (%#x bytes at %#x) is too short", name, dd.Size, dd.VirtualAddress);
    memcpy(&d, p, sizeof d);

    const uint64_t base = img->mapped ? lm->m->base : img->preferred_base, size = img->size;
    const uint64_t start = d.StartAddressOfRawData - base, end = d.EndAddressOfRawData - base;
    const uint64_t index = d.AddressOfIndex - base, cbs = d.AddressOfCallBacks - base;
    if (d.EndAddressOfRawData < d.StartAddressOfRawData || start > size || end > size)
        rt_fatal("%s: its TLS template (%#llx to %#llx) is outside the image at %#llx (an image without "
                 "relocations cannot be used away from its ImageBase)", name, (unsigned long long)d.StartAddressOfRawData,
                 (unsigned long long)d.EndAddressOfRawData, (unsigned long long)base);
    if (index > size - 4)
        rt_fatal("%s: the AddressOfIndex of its TLS directory (%#llx) is outside the image at %#llx", name,
                 (unsigned long long)d.AddressOfIndex, (unsigned long long)base);
    if (d.AddressOfCallBacks && (cbs == 0 || cbs > size - 8))
        rt_fatal("%s: the AddressOfCallBacks of its TLS directory (%#llx) is outside the image at %#llx", name,
                 (unsigned long long)d.AddressOfCallBacks, (unsigned long long)base);
    /* The directory may ask for an alignment of the block (IMAGE_SCN_ALIGN_* in its
     * Characteristics: 2^(v-1) bytes).  More than 16 is not given; said once per module. */
    const unsigned v = (d.Characteristics >> 20) & 0xF;
    if (v > 5) {
        char key[96];
        snprintf(key, sizeof key, "tls-alignment:%s", name);
        rt_log_once(RT_CH_LOADER, RT_LL_WARN, key, "%s: its TLS directory asks for %u-byte alignment of a thread's "
                    "block; blocks are 16-byte aligned", name, 1u << (v - 1));
    }
    out->tpl_rva = (uint32_t)start;
    out->tpl_size = (uint32_t)(end - start);
    out->zero_fill = d.SizeOfZeroFill;
    out->index_rva = (uint32_t)index;
    out->callbacks_rva = d.AddressOfCallBacks ? (uint32_t)cbs : 0;
}

void rt_ldr_tls_prescan(RtLdrModule *lm, const RtImage *img) {
    TlsDir d;
    read_dir(lm, img, &d);
    if (lm->tls_index >= 0) rt_fatal("rt_ldr_tls_prescan(%s) called twice", lm->m->name);
    if (next_index >= RT_STATIC_TLS_SLOTS)
        rt_fatal("%s: more than %d modules have static TLS; the array in the thread block has no slot left "
                 "(RT_STATIC_TLS_SLOTS, rt/win/teb.h)", lm->m->name, RT_STATIC_TLS_SLOTS);
    TlsModule *grown = realloc(tls_mods, ((size_t)next_index + 1) * sizeof *grown);
    if (!grown) rt_fatal("out of memory");
    tls_mods = grown;
    tls_mods[next_index] = (TlsModule){ .lm = lm };
    lm->tls_index = next_index++;
}

/* ---- blocks -------------------------------------------------------------------------------- */

/* The calling guest thread, which must be ctx's. */
static RtThread *own_thread(Ctx *ctx, const char *who) {
    RtThread *t = ctx ? RT_THREAD(ctx) : NULL;
    if (!t || t != rt_thread_self()) rt_fatal("%s: the ctx is not the calling guest thread's", who);
    return t;
}

/* Guest address of slot `index` of t's static TLS array, as guest code finds it:
 * [gs:0x58] + 8 * index. */
static uint64_t slot_of(const RtThread *t, int32_t index) {
    uint64_t array = LD64(t->teb + RT_TEB_TLS_POINTER);
    if (array != t->teb + RT_TEB_STATIC_TLS_ARRAY)
        rt_fatal("thread 0x%x: the static TLS pointer in its thread block is %#llx, not the array the thread "
                 "unit made (%#llx)", t->id, (unsigned long long)array, (unsigned long long)(t->teb + RT_TEB_STATIC_TLS_ARRAY));
    return array + 8 * (uint64_t)index;
}

/* Give thread `t` (any recorded thread, or the one being recorded) its block of a live module. */
static void give_block(const RtThread *t, const TlsModule *tm) {
    const uint64_t slot = slot_of(t, tm->lm->tls_index);
    if (LD64(slot))
        rt_fatal("%s: thread 0x%x already has a static TLS block at index %d", tm->lm->m->name, t->id, tm->lm->tls_index);
    uint64_t size = (uint64_t)tm->tpl_size + tm->zero_fill;
    uint8_t *block = rt_galloc(size ? size : 16);                   /* zeroed: the zero fill is done */
    if ((uintptr_t)block & 15) rt_fatal("%s: a static TLS block at %p is not 16-byte aligned", tm->lm->m->name, (void *)block);
    memcpy(block, tm->lm->img.bytes + tm->tpl_rva, tm->tpl_size);   /* from the image, never the other way */
    ST64(slot, (uint64_t)(uintptr_t)block);
}

void rt_ldr_tls_load(RtLdrModule *lm) {
    RtModule *m = lm->m;
    if (lm->tls_index < 0) rt_fatal("rt_ldr_tls_load(%s): the module has no TLS directory", m->name);
    if (!lm->img.mapped) rt_fatal("rt_ldr_tls_load(%s): the image is not built", m->name);
    TlsModule *tm = &tls_mods[lm->tls_index];
    TlsDir d;
    read_dir(lm, &lm->img, &d);
    if (tm->live) rt_fatal("rt_ldr_tls_load(%s) called twice", m->name);

    ST32(m->base + d.index_rva, (uint32_t)lm->tls_index);
    tm->tpl_rva = d.tpl_rva;
    tm->tpl_size = d.tpl_size;
    tm->zero_fill = d.zero_fill;
    tm->live = true;
    /* Only this module's slot of each thread is written: the blocks a thread already has
     * stay where they are (guest code holds pointers into them). */
    for (RtLdrThread *r = threads; r; r = r->next) give_block(r->thread, tm);
    rt_log(RT_CH_LOADER, RT_LL_INFO, "%s: static TLS index %d, template %#x bytes + %#x zero, %u thread%s given a block",
           m->name, lm->tls_index, d.tpl_size, d.zero_fill, nthreads, nthreads == 1 ? "" : "s");
}

/* ---- threads ------------------------------------------------------------------------------- */

RtLdrThread *rt_ldr_thread_record(const RtThread *t) {
    return t ? __atomic_load_n((RtLdrThread *const *)&t->sub[RT_SUB_LOADER], __ATOMIC_RELAXED) : NULL;
}

unsigned rt_ldr_thread_count(void) { return nthreads; }

void rt_ldr_tls_thread(Ctx *ctx) {
    RtThread *t = own_thread(ctx, "rt_ldr_tls_thread");
    if (rt_ldr_thread_record(t)) return;
    RtLdrThread *r = calloc(1, sizeof *r);
    if (!r) rt_fatal("out of memory");
    r->thread = t;
    for (int32_t i = 0; i < next_index; i++)
        if (tls_mods[i].live) give_block(t, &tls_mods[i]);
    r->next = threads;
    threads = r;
    nthreads++;
    __atomic_store_n((RtLdrThread **)&t->sub[RT_SUB_LOADER], r, __ATOMIC_RELAXED);    /* last: the record is complete */
}

void rt_ldr_tls_thread_end(Ctx *ctx) {
    RtThread *t = own_thread(ctx, "rt_ldr_tls_thread_end");
    RtLdrThread *r = rt_ldr_thread_record(t);
    if (!r) rt_fatal("rt_ldr_tls_thread_end: thread 0x%x is not recorded with the loader", t->id);
    RtLdrThread **link = &threads;
    while (*link && *link != r) link = &(*link)->next;
    if (!*link) rt_fatal("rt_ldr_tls_thread_end: the record of thread 0x%x is not in the loader's list", t->id);
    *link = r->next;                    /* from here on no load reaches this thread's array */
    nthreads--;
    for (int32_t i = 0; i < next_index; i++) {
        const uint64_t slot = slot_of(t, i);
        void *block = (void *)(uintptr_t)LD64(slot);
        if (!block) continue;
        ST64(slot, 0);
        rt_gfree(block);
    }
    __atomic_store_n((RtLdrThread **)&t->sub[RT_SUB_LOADER], NULL, __ATOMIC_RELAXED);  /* before the free */
    free(r);
}

/* ---- entry points and TLS callbacks ---------------------------------------------------------- */

uint64_t rt_ldr_entry_enter(Ctx *ctx, RtLdrModule *lm, DWORD reason) {
    RtLdrThread *r = rt_ldr_thread_record(own_thread(ctx, "rt_ldr_entry_enter"));
    if (!r) rt_fatal("rt_ldr_entry_enter(%s): the calling thread is not recorded with the loader", lm->m->name);
    if (reason > 3) rt_fatal("rt_ldr_entry_enter(%s): reason %u is not a DllMain reason", lm->m->name, reason);
    uint64_t outer = __atomic_load_n(&r->entry, __ATOMIC_RELAXED);
    __atomic_store_n(&r->entry, (uint64_t)(uintptr_t)lm->m | reason, __ATOMIC_RELAXED);
    return outer;
}

void rt_ldr_entry_leave(Ctx *ctx, uint64_t outer) {
    RtLdrThread *r = rt_ldr_thread_record(own_thread(ctx, "rt_ldr_entry_leave"));
    if (!r) rt_fatal("rt_ldr_entry_leave: the calling thread is not recorded with the loader");
    __atomic_store_n(&r->entry, outer, __ATOMIC_RELAXED);
}

bool rt_loader_in_entry(const RtThread *t, const RtModule **m, DWORD *reason) {
    const RtLdrThread *r = rt_ldr_thread_record(t);
    uint64_t e = r ? __atomic_load_n(&r->entry, __ATOMIC_RELAXED) : 0;
    if (!e) return false;
    if (m) *m = (const RtModule *)(uintptr_t)(e & ~UINT64_C(3));
    if (reason) *reason = (DWORD)(e & 3);
    return true;
}

void rt_ldr_tls_callbacks(Ctx *ctx, RtLdrModule *lm, DWORD reason) {
    RtModule *m = lm->m;
    if (lm->tls_index < 0) return;                      /* no TLS directory */
    if (!lm->img.mapped) rt_fatal("rt_ldr_tls_callbacks(%s): the image is not built", m->name);
    TlsDir d;
    read_dir(lm, &lm->img, &d);
    if (!d.callbacks_rva) return;
    rt_ldr_tls_thread(ctx);                             /* the mark needs the record */
    for (uint32_t i = 0;; i++) {
        /* Read at each step, as Windows walks the array: a callback may not have been
         * there when the walk began. */
        uint64_t at = (uint64_t)d.callbacks_rva + 8 * (uint64_t)i;
        const void *p = at <= UINT32_MAX ? rt_ldr_rva(&lm->img, (uint32_t)at, 8) : NULL;
        if (!p) rt_fatal("%s: its TLS callback array (at %#x) runs outside the image: it has no terminator", m->name, d.callbacks_rva);
        uint64_t fn;
        memcpy(&fn, p, 8);
        if (!fn) break;
        const uint64_t args[3] = { m->base, reason, 0 };
        uint64_t outer = rt_ldr_entry_enter(ctx, lm, reason);
        (void)rt_guest_call(ctx, fn, 3, args);
        rt_ldr_entry_leave(ctx, outer);
    }
}
