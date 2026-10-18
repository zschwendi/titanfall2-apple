/* The seam between the do-nothing device (the front) and a back end: choosing the back end,
 * the back pointers, the seam's counts, its stops, and the guard of the context (unit pic-seam;
 * rt/d3dback.h has the rules, rt/nulldev.h the records). */
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/log.h"
#include "rt/module.h"
#include "rt/nulldev.h"
#include "rt/thread.h"

/* ---- the entries ------------------------------------------------------------------------------------- */

#define NAME_(entry, iface) #entry,
#define IFACE_(entry, iface) #iface,
static const char *const entry_name[RT_BACK_N] = { RT_D3DBACK_ENTRIES(NAME_) };
static const char *const entry_iface[RT_BACK_N] = { RT_D3DBACK_ENTRIES(IFACE_) };
#undef NAME_
#undef IFACE_

/* The table's members stand in the order of the list: entry i is the i-th function pointer. */
#define OFFSET_(entry, iface) \
    _Static_assert(offsetof(RtD3dBack, entry) == sizeof(const char *) + RT_BACK_##entry * sizeof(void (*)(void)), \
                   "RtD3dBack." #entry " is not where RT_D3DBACK_ENTRIES has it");
RT_D3DBACK_ENTRIES(OFFSET_)
#undef OFFSET_

const char *rt_d3dback_entry_name(RtD3dBackEntry entry) { return (unsigned)entry < RT_BACK_N ? entry_name[entry] : "?"; }

const char *rt_d3dback_entry_method(RtD3dBackEntry entry) {
    static char text[RT_BACK_N][72];            /* filled once per entry; the same text whoever fills it */
    if ((unsigned)entry >= RT_BACK_N || entry_iface[entry][0] == '-') return NULL;
    if (!text[entry][0]) snprintf(text[entry], sizeof text[entry], "%s::%s", entry_iface[entry], entry_name[entry]);
    return text[entry];
}

/* ---- choosing ------------------------------------------------------------------------------------------ */

_Atomic(const RtD3dBack *) rt_d3dback_active;
static _Atomic int chosen;                      /* 0 not yet; 1 by rt_d3dback_use; 2 by the environment, or a device exists */

static void report(void);

static void complete(const RtD3dBack *b) {
    void (*const *fn)(void) = (void (*const *)(void))(const void *)((const char *)b + sizeof(const char *));
    if (!b->name) rt_fatal("rt_d3dback: a back end with no name");
    for (unsigned i = 0; i < RT_BACK_N; i++)
        if (!fn[i]) rt_fatal("rt_d3dback: the back end %s has no entry %s; a table sets every entry (rt/d3dback.h)", b->name, entry_name[i]);
}

void rt_d3dback_use(const RtD3dBack *back) {
    if (atomic_load(&chosen) == 2 && back != rt_d3dback())
        rt_fatal("rt_d3dback_use: a device was already made with the back end %s; the choice is made before the first device",
                 rt_d3dback() ? rt_d3dback()->name : "none");
    if (back) complete(back);
    atomic_store(&rt_d3dback_active, back);
    atomic_store(&chosen, 1);
    if (back) rt_com_counts_also(report);
}

/* The DXMT back end: runtime/dxback/dxback.cpp, linked into the replay player only (runtime/replay.mk). */
/* ld64 refuses an undefined weak reference in an executable, so the default is a weak definition: NULL, which the
 * DXMT object's strong definition replaces where it is linked. */
__attribute__((weak)) const RtD3dBack *rt_dxback_table(void) { return NULL; }

void rt_d3dback_select(void) {
    int was = atomic_load(&chosen);
    if (was == 2) return;
    if (was == 0) {
        const char *e = getenv("RT_D3D_BACKEND");
        if (!e || !*e || !strcmp(e, "none")) rt_d3dback_use(NULL);
        else if (!strcmp(e, "record")) rt_d3dback_use(rt_d3dback_record());
        else if (!strcmp(e, "dxmt")) {
            const RtD3dBack *dx = rt_dxback_table();                  /* runtime/dxback/dxback.cpp, where it is linked */
            if (dx) rt_d3dback_use(dx);
            else rt_fatal("RT_D3D_BACKEND=dxmt: this program was linked without the DXMT back end (runtime/dxback/, built into "
                          "build/runtime/d3d-replay); the back ends that exist here are none (the default) and record");
        }
        else rt_fatal("RT_D3D_BACKEND=%s: the values are none (the default), record and dxmt", e);
    }
    atomic_store(&chosen, 2);
    if (rt_d3dback()) rt_log(RT_CH_COM, RT_LL_INFO, "d3d: back end %s behind the device (RT_D3D_BACKEND)", rt_d3dback()->name);
}

/* ---- back pointers ------------------------------------------------------------------------------------- */

/* Where the back pointer of the live front object `front` is kept, or NULL when its class has none. */
static void **slot_of(void *front, const RtComClass **cls_out) {
    const RtComClass *cls = rt_com_class_of(front);
    if (cls_out) *cls_out = cls;
    if (!cls) rt_fatal("rt_d3dback: %p is not a live object of the interface layer", front);
    int kind = rt_nulldev_back_kind(cls);
    if (kind < 0) return NULL;
    void *state = rt_com_state_of(front, cls);
    return kind == RT_BK_DEVICE ? &((NdDevice *)state)->back : &((NdChild *)state)->back;
}

/* A back pointer is written once, by the creator, and creators may run on several threads: the
 * slot goes NULL -> CLAIMED (the creator is asking the back end) -> the object, each step atomic. */
#define CLAIMED ((void *)(uintptr_t)1)
static void *slot_load(void **slot) { return __atomic_load_n(slot, __ATOMIC_ACQUIRE); }

void *rt_d3dback_of(void *front) {
    if (!front) return NULL;
    const RtComClass *cls;
    void **slot = slot_of(front, &cls);
    if (!slot) rt_fatal("rt_d3dback_of: an object of class %s has no back object (rt/nulldev.h, ND_BACK_KINDS)", cls->name);
    void *back = slot_load(slot);
    while (back == CLAIMED) { sched_yield(); back = slot_load(slot); }      /* its creator is attaching it on another thread */
    if (!back && rt_d3dback())
        rt_fatal("rt_d3dback_of: object #%llu of class %s has no back object although the back end %s is there: its creator did not "
                 "ask for one (rt/d3dback.h, rules 2 and 3)", (unsigned long long)rt_com_serial(front), cls->name, rt_d3dback()->name);
    return back;
}

bool rt_d3dback_claim(void *front) {
    const RtComClass *cls;
    void **slot = slot_of(front, &cls), *none = NULL;
    if (!slot) rt_fatal("rt_d3dback_claim: an object of class %s has no back object (rt/nulldev.h, ND_BACK_KINDS)", cls->name);
    return __atomic_compare_exchange_n(slot, &none, CLAIMED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

void rt_d3dback_attach(void *front, void *back) {
    const RtComClass *cls;
    void **slot = slot_of(front, &cls);
    void *was = slot ? slot_load(slot) : NULL;
    if (!slot || !back || back == CLAIMED || (was && was != CLAIMED))
        rt_fatal("rt_d3dback_attach: object #%llu of class %s %s", (unsigned long long)rt_com_serial(front), cls->name,
                 !slot ? "is of a class with no back object" : was ? "has a back object already" : "was given no back object");
    __atomic_store_n(slot, back, __ATOMIC_RELEASE);
}

/* ---- the seam's counts, and which call reached the back end last ----------------------------------------- */

static _Atomic uint64_t asked[RT_BACK_N], alone[RT_BACK_N];
/* The last call that reached the back end: its method and the number of that call (the method's
 * count of calls when it reached).  A stop inside the same call of the same method finds both
 * unchanged.  Two words written without a lock: a report that reads them while another thread
 * forwards can only name a call that did reach the back end. */
static _Atomic(const RtComMethod *) last_method;
static _Atomic uint64_t last_nth;
static _Atomic int last_entry = -1;

static const RtComMethod *method_in(const RtThread *t, uint64_t *nth) {
    const RtShimEntry *e = t && t->shim ? t->shim->shim : NULL;
    if (!e) return NULL;
    /* Is this row a method's?  A method's row is the first member of an RtComMethod whose
     * interface lists it at its slot; an import's row is not.  The name tells them apart
     * without reading past a shim's row: only a method's has "::". */
    if (!e->name || !strstr(e->name, "::")) return NULL;
    const RtComMethod *m = (const RtComMethod *)(const void *)e;
    *nth = atomic_load_explicit(&m->iface->counts[m->slot].calls, memory_order_relaxed);
    return m;
}

void rt_d3dback_reach(Ctx *ctx, RtD3dBackEntry entry) {
    atomic_fetch_add_explicit(&asked[entry], 1, memory_order_relaxed);
    uint64_t nth = 0;
    const RtComMethod *m = method_in(ctx ? RT_THREAD(ctx) : NULL, &nth);
    atomic_store_explicit(&last_method, m, memory_order_relaxed);
    atomic_store_explicit(&last_nth, nth, memory_order_relaxed);
    atomic_store_explicit(&last_entry, (int)entry, memory_order_relaxed);
}

void rt_d3dback_alone_(Ctx *ctx, RtD3dBackEntry entry) {
    (void)ctx;
    atomic_fetch_add_explicit(&alone[entry], 1, memory_order_relaxed);
}

uint64_t rt_d3dback_asked(RtD3dBackEntry entry) { return (unsigned)entry < RT_BACK_N ? atomic_load(&asked[entry]) : 0; }
uint64_t rt_d3dback_alone(RtD3dBackEntry entry) { return (unsigned)entry < RT_BACK_N ? atomic_load(&alone[entry]) : 0; }

void rt_d3dback_release(RtD3dBackKind kind, void **back) {
    void *b = *back;
    if (!b) return;
    *back = NULL;
    if (b == CLAIMED) return;                   /* its creator stopped between the claim and the attach: nothing was made */
    const RtD3dBack *be = rt_d3dback();
    if (!be) rt_fatal("rt_d3dback_release: a front object has a back object (%p, kind %u) and no back end is there", b, (unsigned)kind);
    atomic_fetch_add_explicit(&asked[RT_BACK_Release], 1, memory_order_relaxed);
    be->Release(kind, b);
}

void rt_d3dback_differs(Ctx *ctx, RtD3dBackEntry entry, HRESULT front, HRESULT back) {
    const char *method = rt_d3dback_entry_method(entry);
    RT_UNIMPLEMENTED(ctx, "the back end (%s) answered 0x%08x to %s, where the front answered 0x%08x: the two must agree, or the "
                     "front must stop first (rt/d3dback.h, rule 4)", rt_d3dback() ? rt_d3dback()->name : "none", (unsigned)back,
                     method ? method : rt_d3dback_entry_name(entry), (unsigned)front);
}

/* ---- the guard of the context (rt/nulldev.h, NdGuard; rt/d3dback.h, rule 8) ------------------------------ */

static _Atomic uint64_t guard_collisions;
static _Atomic int guard_mode = -1;             /* 1: count; 0: stop */
/* What the report prints: the threads of the device that was called last (a run has one). */
static _Atomic(NdGuard *) guard_seen;

static bool guard_counts(void) {
    int v = atomic_load(&guard_mode);
    if (v < 0) {
        const char *e = getenv("RT_D3D_GUARD");
        v = e && !strcmp(e, "count");
        atomic_store(&guard_mode, v);
    }
    return v;
}

/* The calling thread is not the one of the call before (the caller is inside: nobody else writes). */
static void guard_note(NdGuard *g, uint32_t me) {
    g->last = me;
    for (unsigned i = 0; i < g->nthreads && i < ND_GUARD_THREADS; i++)
        if (g->threads[i].id == me) { g->last_calls = &g->threads[i].calls; ++*g->last_calls; return; }
    g->last_calls = &g->others;                 /* a ninth thread and later ones are counted together */
    if (g->nthreads < ND_GUARD_THREADS) { g->threads[g->nthreads].id = me; g->last_calls = &g->threads[g->nthreads].calls; }
    ++*g->last_calls;
    g->nthreads++;
    if (g->nthreads == 1) { atomic_store(&guard_seen, g); rt_com_counts_also(report); }
    else rt_log(RT_CH_COM, RT_LL_INFO, "d3d: the context is called from thread 0x%x, the %u%s thread to call it", (unsigned)me,
                (unsigned)g->nthreads, g->nthreads == 2 ? "nd" : g->nthreads == 3 ? "rd" : "th");
}

static NdGuard *guard_of(Ctx *ctx, void *This) {
    const RtComClass *cls = rt_com_class_of(This);
    ID3D11Device *device;
    if (cls == &rt_nulldev_context_class) device = ((NdContext *)rt_com_state(ctx, This, cls))->child.device;
    else device = ((NdSwapChain *)rt_com_state(ctx, This, &rt_nulldev_swapchain_class))->device;     /* stops on anything else */
    return &((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->guard;
}

void *rt_nulldev_guard_enter(Ctx *ctx, void *This, const RtComMethod *m) {
    NdGuard *g = guard_of(ctx, This);
    RtThread *t = RT_THREAD(ctx);
    uint32_t me = t->id, was = 0;
    if (atomic_compare_exchange_strong_explicit(&g->inside, &was, me, memory_order_acquire, memory_order_relaxed)) {
        g->depth = 1;
        g->frame = t->shim;
        g->method = m;
        if (__builtin_expect(g->last == me, 1)) ++*g->last_calls;
        else guard_note(g, me);
        return g;
    }
    if (was == me) {
        /* This thread is inside already: a call made from within one, unless the earlier call was
         * left without returning (a guest exception unwound through it), which its frame tells:
         * it is then no longer in the thread's chain of shim frames. */
        for (const RtShimFrame *f = t->shim; f; f = f->prev)
            if (f == g->frame) { g->depth++; return g; }
        rt_log(RT_CH_COM, RT_LL_WARN, "d3d: thread 0x%x left %s::%s without returning (an unwind); the guard of the context starts again",
               (unsigned)me, g->method ? g->method->iface->name : "?", g->method ? g->method->name : "?");
        g->depth = 1;
        g->frame = t->shim;
        g->method = m;
        return g;
    }
    /* A second thread while one is inside.  (g->method is the other thread's to write: read here
     * for the report only.) */
    const RtComMethod *other = g->method;
    uint64_t n = atomic_fetch_add(&guard_collisions, 1);
    if (!guard_counts())
        RT_UNIMPLEMENTED(ctx, "a second thread in the immediate context: thread 0x%x enters %s::%s while thread 0x%x is inside %s::%s. "
                         "Nothing locks the context, in Direct3D or in a renderer behind this device (rt/d3dback.h, rule 8); "
                         "RT_D3D_GUARD=count counts such entries instead of stopping", (unsigned)me, m->iface->name, m->name,
                         (unsigned)was, other ? other->iface->name : "?", other ? other->name : "?");
    if (!n) {
        char where[160];
        rt_log(RT_CH_COM, RT_LL_WARN, "d3d: a second thread in the immediate context (counted, RT_D3D_GUARD=count): thread 0x%x enters "
               "%s::%s from %s while thread 0x%x is inside %s::%s", (unsigned)me, m->iface->name, m->name,
               rt_module_describe(LD64(ctx->r[4]), where, sizeof where), (unsigned)was, other ? other->iface->name : "?",
               other ? other->name : "?");
    }
    return NULL;
}

void rt_nulldev_guard_leave(void *guard) {
    NdGuard *g = guard;
    if (!g) return;
    if (--g->depth == 0) atomic_store_explicit(&g->inside, 0, memory_order_release);
}

unsigned rt_nulldev_guard_stats(ID3D11Device *device, uint64_t *collisions) {
    if (collisions) *collisions = atomic_load(&guard_collisions);
    return ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->guard.nthreads;
}

/* ---- the report: printed with the call table, at the end of every fatal report and at exit --------------- */

static void report(void) {
    static char text[4096];             /* static: a fatal report may be on a small signal stack */
    size_t at = 0;
    NdGuard *g = atomic_load(&guard_seen);
    if (g) {
        at += (size_t)snprintf(text + at, sizeof text - at, "d3d: the context was called from %u thread%s:", (unsigned)g->nthreads,
                               g->nthreads == 1 ? "" : "s");
        for (unsigned i = 0; i < g->nthreads && i < ND_GUARD_THREADS && at < sizeof text; i++)
            at += (size_t)snprintf(text + at, sizeof text - at, " 0x%x (%llu calls)", (unsigned)g->threads[i].id,
                                   (unsigned long long)g->threads[i].calls);
        if (g->others && at < sizeof text)
            at += (size_t)snprintf(text + at, sizeof text - at, " others (%llu calls)", (unsigned long long)g->others);
        if (at < sizeof text)
            at += (size_t)snprintf(text + at, sizeof text - at, "; a second thread entered while one was inside: %llu times%s\n",
                                   (unsigned long long)atomic_load(&guard_collisions), guard_counts() ? " (RT_D3D_GUARD=count)" : "");
    }
    const RtD3dBack *b = rt_d3dback();
    if (b && at < sizeof text) {
        uint64_t total = 0, kinds = 0;
        for (unsigned i = 0; i < RT_BACK_N; i++) { total += atomic_load(&asked[i]); kinds += atomic_load(&asked[i]) != 0; }
        at += (size_t)snprintf(text + at, sizeof text - at, "d3d: back end %s: %llu calls handed on, of %llu of the %u entries\n", b->name,
                               (unsigned long long)total, (unsigned long long)kinds, (unsigned)RT_BACK_N);
        if (b == rt_d3dback_record() && at < sizeof text) {
            uint64_t calls = 0, made = 0, live = 0;
            for (unsigned i = 0; i < RT_BACK_N; i++) calls += rt_d3dback_record_count((RtD3dBackEntry)i);
            for (unsigned k = 0; k < RT_BK_KINDS; k++) { made += rt_d3dback_record_made((RtD3dBackKind)k); live += rt_d3dback_record_live((RtD3dBackKind)k); }
            at += (size_t)snprintf(text + at, sizeof text - at, "d3dback: record: %llu calls reached the back end; %llu of its objects made, "
                                   "%llu alive\n", (unsigned long long)calls, (unsigned long long)made, (unsigned long long)live);
        }
        /* Rule 1's line.  Only a thread that is inside a method can have broken it. */
        uint64_t nth = 0;
        const RtComMethod *m = method_in(rt_thread_self(), &nth);
        int e = atomic_load(&last_entry);
        if (m && at < sizeof text) {
            if (m == atomic_load(&last_method) && nth == atomic_load(&last_nth) && e >= 0)
                at += (size_t)snprintf(text + at, sizeof text - at, "d3dback: RULE BROKEN: the call that stopped (%s::%s) had reached "
                                       "the back end already (entry %s); a body forwards after its last check (rt/d3dback.h, rule 1)\n",
                                       m->iface->name, m->name, entry_name[e]);
            else
                at += (size_t)snprintf(text + at, sizeof text - at, "d3dback: the call that stopped had not reached the back end\n");
        }
    }
    if (at) rt_log_raw(text, at < sizeof text ? at : sizeof text - 1);
}
