/* The interface layer (docs/RUNTIME.md section 3.7).  Interface: rt/com.h.
 *
 * Nothing here knows an interface by name: the descriptions are generated tables, and what a
 * method does is in its body file (or, at Gate 4, a proxy).  This file is what every object
 * shares: registration of vtables and stubs, the object block, the reference count,
 * QueryInterface, the stop of a method that has no body, and the call counts.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#include "core.h"
#include "report.h"
#include "rt/com.h"
#include "rt/heap.h"

/* ---- registration ------------------------------------------------------------------------------- */

static RtComTable *tables;          /* written in rt_init only; read without a lock afterwards */

/* The vtable area of a native module: 8-byte slots handed out upwards from +0x80000. */
#define VT_MODULES 8
static struct { const RtModule *m; uint64_t used; } vt_area[VT_MODULES];

static uint64_t vtable_alloc(const RtModule *m, unsigned nslots) {
    unsigned i = 0;
    while (i < VT_MODULES && vt_area[i].m && vt_area[i].m != m) i++;
    if (i == VT_MODULES) rt_fatal("rt_com_register: more than %d modules hold vtables", VT_MODULES);
    vt_area[i].m = m;
    uint64_t at = vt_area[i].used;
    if (at + 8 * (uint64_t)nslots > RT_NATIVE_SLOT - RT_NATIVE_VTABLES)
        rt_fatal("%s has no room for another vtable of %u slots", m->name, nslots);
    vt_area[i].used = at + 8 * (uint64_t)nslots;
    return m->base + RT_NATIVE_VTABLES + at;
}

/* An all-zero IID is no IID: a root interface (no IUnknown, no uuid) has it, and an IID that
 * is zero is never asked for or matched.  Rules that compare IIDs skip it. */
static bool iid_none(const GUID *g) {
    static const GUID zero;
    return !memcmp(g, &zero, sizeof zero);
}

const RtComIface *rt_com_iface_by_iid(const GUID *iid) {
    if (iid_none(iid)) return NULL;
    for (const RtComTable *t = tables; t; t = t->next)
        for (unsigned i = 0; i < t->nifaces; i++)
            if (!iid_none(&t->ifaces[i]->iid) && !memcmp(&t->ifaces[i]->iid, iid, sizeof *iid)) return t->ifaces[i];
    return NULL;
}

const RtComIface *rt_com_iface_by_name(const char *name) {
    for (const RtComTable *t = tables; t; t = t->next)
        for (unsigned i = 0; i < t->nifaces; i++)
            if (!strcmp(t->ifaces[i]->name, name)) return t->ifaces[i];
    return NULL;
}

void rt_com_register(RtComTable *t) {
    if (rt_disp_sealed())
        rt_fatal("rt_com_register(%s) after the dispatch table was sealed; register during rt_init "
                 "(RtConfig.before_seal)", t->what);
    for (const RtComTable *o = tables; o; o = o->next)
        if (o == t) rt_fatal("rt_com_register(%s): registered twice", t->what);
    for (unsigned i = 0; i < t->nifaces; i++) {
        RtComIface *it = t->ifaces[i];
        if (rt_com_iface_by_name(it->name) || rt_com_iface_by_iid(&it->iid))
            rt_fatal("rt_com_register(%s): interface %s (or its IID) is already registered", t->what, it->name);
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(t->ifaces[j]->name, it->name) ||
                (!iid_none(&it->iid) && !memcmp(&t->ifaces[j]->iid, &it->iid, sizeof it->iid)))
                rt_fatal("rt_com_register(%s): interface %s (or its IID) is described twice", t->what, it->name);
        RtModule *m = rt_module_find(it->owner);
        if (!m || !m->native)
            rt_fatal("rt_com_register(%s): %s names the owner %s, which is not a native module of the manifest",
                     t->what, it->name, it->owner);
        it->vtable = vtable_alloc(m, it->nmethods);
        for (unsigned s = 0; s < it->nmethods; s++) {
            const RtComMethod *me = &it->methods[s];
            ST64(it->vtable + 8 * (uint64_t)s, rt_native_stub(m, me->entry.name, me->entry.thunk));
        }
    }
    t->next = tables;
    tables = t;
}

void rt_com_init(void) {
    rt_com_register(&rt_com_table);
}

/* ---- the object block ----------------------------------------------------------------------------
 *
 *     RtComObject   header: class, reference count, creation number
 *     RtComFace[n]  what the guest holds: +0 the vtable address, as lifted code expects
 *     state         cls->state_size bytes, 16-byte aligned
 *
 * all in one rt_galloc block.  A dead object keeps its block for ever (rt/com.h, Release). */

#define FACE_LIVE  0x4D4F4374u      /* "tCOM" */
#define FACE_DEAD  0x44414544u      /* "DEAD" */

struct RtComObject;

typedef struct RtComFace {
    uint64_t            vtbl;       /* +0   guest address of the interface's vtable */
    _Atomic uint32_t    magic;      /* +8   FACE_LIVE, FACE_DEAD */
    uint32_t            index;      /* +12  which face of the object */
    struct RtComObject *obj;        /* +16 */
    uint64_t            check;      /* +24  the face's own address xor FACE_CHECK */
} RtComFace;
#define FACE_CHECK  UINT64_C(0x52745F636F6D5F66)

_Static_assert(offsetof(RtComFace, vtbl) == 0, "the guest reads the vtable address at offset 0");
_Static_assert(sizeof(RtComFace) == 32, "rt_com_class_of reads 32 bytes");

typedef struct RtComObject {
    const RtComClass *cls;
    _Atomic uint64_t  count;        /* low 32 bits: every reference, the object lives while it is not 0;
                                       high 32 bits: how many of them are HOLDS (rt_com_hold), which the
                                       guest's AddRef and Release do not report.  One word, so that each
                                       change and the test for 0 are one atomic step */
    uint32_t          nfaces;
    uint64_t          serial;
    void             *state;
    RtComFace         face[];
} RtComObject;

static _Atomic uint64_t next_serial;

/* True if face `f` answers for the interface `name`: it presents it or derives from it. */
static bool face_is(const RtComIface *it, const RtComIface *want) {
    if (it == want) return true;
    if (iid_none(&want->iid)) return false;
    for (unsigned i = 0; i < it->nchain; i++)
        if (!memcmp(&it->chain[i].iid, &want->iid, sizeof want->iid)) return true;
    return false;
}

static bool face_answers(const RtComIface *it, const GUID *iid) {
    if (iid_none(iid)) return false;
    if (!memcmp(&it->iid, iid, sizeof *iid)) return true;
    for (unsigned i = 0; i < it->nchain; i++)
        if (!memcmp(&it->chain[i].iid, iid, sizeof *iid)) return true;
    return false;
}

void *rt_com_new(const RtComClass *cls) {
    if (!cls || cls->nfaces == 0) rt_fatal("rt_com_new: class %s has no face", cls ? cls->name : "(null)");
    for (unsigned i = 0; i < cls->nfaces; i++) {
        const RtComIface *it = cls->faces[i];
        if (!it->vtable) rt_fatal("rt_com_new(%s): interface %s is not registered", cls->name, it->name);
        for (unsigned j = 0; j < i; j++)
            if (face_is(it, cls->faces[j]) || face_is(cls->faces[j], it))
                rt_fatal("rt_com_new(%s): faces %s and %s answer for the same interface", cls->name,
                         cls->faces[j]->name, it->name);
    }
    size_t head = (sizeof(RtComObject) + cls->nfaces * sizeof(RtComFace) + 15) & ~(size_t)15;
    RtComObject *o = rt_galloc(head + cls->state_size);
    o->cls = cls;
    o->nfaces = cls->nfaces;
    o->serial = atomic_fetch_add(&next_serial, 1) + 1;
    o->state = cls->state_size ? (char *)o + head : NULL;
    atomic_store(&o->count, 1);
    for (unsigned i = 0; i < cls->nfaces; i++) {
        RtComFace *f = &o->face[i];
        f->vtbl = cls->faces[i]->vtable;
        f->index = i;
        f->obj = o;
        f->check = (uint64_t)(uintptr_t)f ^ FACE_CHECK;
        atomic_store(&f->magic, FACE_LIVE);
    }
    return &o->face[0];
}

/* `p` as a face of this layer, live or dead, or NULL. */
static RtComFace *as_face(const void *p) {
    RtComFace *f = (RtComFace *)(uintptr_t)p;
    if (!f || ((uintptr_t)f & 7)) return NULL;
    if (f->check != ((uint64_t)(uintptr_t)f ^ FACE_CHECK)) return NULL;
    uint32_t magic = atomic_load(&f->magic);
    return magic == FACE_LIVE || magic == FACE_DEAD ? f : NULL;
}

const RtComClass *rt_com_class_of(const void *p) {
    RtComFace *f = as_face(p);
    return f && atomic_load(&f->magic) == FACE_LIVE ? f->obj->cls : NULL;
}

/* For host callers (not inside a method): a face of a live object, or a fatal error. */
static RtComFace *live_face(void *any, const char *who) {
    RtComFace *f = as_face(any);
    if (!f) rt_fatal("%s: %p is not an object of the interface layer", who, any);
    if (atomic_load(&f->magic) != FACE_LIVE)
        rt_fatal("%s: object #%llu of class %s was destroyed (its last reference was released)", who,
                 (unsigned long long)f->obj->serial, f->obj->cls->name);
    return f;
}

/* Inside a method: the same, as a stop that names the method the thread is in. */
static RtComFace *this_face(Ctx *ctx, void *This) {
    RtComFace *f = as_face(This);
    if (!f) RT_UNIMPLEMENTED(ctx, "`this` (%p) is not an object of the interface layer", This);
    if (atomic_load(&f->magic) != FACE_LIVE)
        RT_UNIMPLEMENTED(ctx, "`this` (%p) is object #%llu of class %s, which was destroyed: its last reference "
                         "was released earlier", This, (unsigned long long)f->obj->serial, f->obj->cls->name);
    return f;
}

void *rt_com_face(void *any, const RtComIface *iface) {
    RtComObject *o = live_face(any, "rt_com_face")->obj;
    for (unsigned i = 0; i < o->nfaces; i++)
        if (face_is(o->cls->faces[i], iface)) return &o->face[i];
    return NULL;
}

void *rt_com_state(Ctx *ctx, void *This, const RtComClass *cls) {
    RtComObject *o = this_face(ctx, This)->obj;
    if (o->cls != cls)
        RT_UNIMPLEMENTED(ctx, "`this` (%p) is object #%llu of class %s, where an object of class %s is expected",
                         This, (unsigned long long)o->serial, o->cls->name, cls->name);
    return o->state;
}

void *rt_com_state_of(void *any, const RtComClass *cls) {
    RtComObject *o = live_face(any, "rt_com_state_of")->obj;
    if (o->cls != cls)
        rt_fatal("rt_com_state_of: %p is object #%llu of class %s, where an object of class %s is expected", any,
                 (unsigned long long)o->serial, o->cls->name, cls->name);
    return o->state;
}

#define ONE_REF   UINT64_C(1)
#define ONE_HOLD  (UINT64_C(1) << 32 | 1)       /* a hold is a reference that is also counted as ours */

static int32_t total_of(uint64_t c) { return (int32_t)(uint32_t)c; }
static int32_t holds_of(uint64_t c) { return (int32_t)(uint32_t)(c >> 32); }

LONG     rt_com_refs(void *any)        { return total_of(atomic_load(&live_face(any, "rt_com_refs")->obj->count)); }
LONG     rt_com_public_refs(void *any) {
    uint64_t c = atomic_load(&live_face(any, "rt_com_public_refs")->obj->count);
    return total_of(c) - holds_of(c);
}
uint64_t rt_com_serial(void *any) { return live_face(any, "rt_com_serial")->obj->serial; }

static uint64_t add(RtComObject *o, uint64_t unit) { return atomic_fetch_add(&o->count, unit) + unit; }

/* Take one reference (unit ONE_REF: one that is not a hold) or one hold away.  False, and
 * nothing changed, when the object has none of that kind.  At 0 the object is destroyed. */
static bool take(RtComObject *o, uint64_t unit, uint64_t *after) {
    uint64_t c = atomic_load(&o->count);
    do {
        if (unit == ONE_HOLD ? holds_of(c) <= 0 : total_of(c) <= holds_of(c)) return false;
    } while (!atomic_compare_exchange_weak(&o->count, &c, c - unit));
    *after = c - unit;
    if (total_of(*after)) return true;
    if (o->cls->destroy) o->cls->destroy(o->state);
    for (unsigned i = 0; i < o->nfaces; i++) atomic_store(&o->face[i].magic, FACE_DEAD);
    rt_log(RT_CH_COM, RT_LL_DEBUG, "object #%llu of class %s destroyed", (unsigned long long)o->serial, o->cls->name);
    return true;
}

ULONG rt_com_addref(void *any) { return (ULONG)total_of(add(live_face(any, "rt_com_addref")->obj, ONE_REF)); }
ULONG rt_com_hold(void *any)   { return (ULONG)total_of(add(live_face(any, "rt_com_hold")->obj, ONE_HOLD)); }

bool rt_com_addref_live(void *any) {
    RtComObject *o = live_face(any, "rt_com_addref_live")->obj;
    uint64_t c = atomic_load(&o->count);
    do {
        if (!total_of(c)) return false;         /* its last reference is gone: its destroy is running on another thread */
    } while (!atomic_compare_exchange_weak(&o->count, &c, c + ONE_REF));
    return true;
}

ULONG rt_com_release(void *any) {
    RtComObject *o = live_face(any, "rt_com_release")->obj;
    uint64_t c;
    if (!take(o, ONE_REF, &c))
        rt_fatal("rt_com_release: object #%llu of class %s has no reference left but the runtime's own %d hold(s)",
                 (unsigned long long)o->serial, o->cls->name, (int)holds_of(atomic_load(&o->count)));
    return (ULONG)total_of(c);
}

ULONG rt_com_unhold(void *any) {
    RtComObject *o = live_face(any, "rt_com_unhold")->obj;
    uint64_t c;
    if (take(o, ONE_HOLD, &c)) return (ULONG)total_of(c);
    /* No hold: the record was given this object with rt_com_addref (a test that fills a record by
     * hand, as the task rows of 10 October told it to).  The reference is released as what it is,
     * and said aloud: in a run this line means a body and a destroy function disagree. */
    rt_log(RT_CH_COM, RT_LL_WARN, "rt_com_unhold: object #%llu of class %s has no hold; a plain reference is released instead",
           (unsigned long long)o->serial, o->cls->name);
    return rt_com_release(any);
}

/* ---- the three methods of IUnknown ----------------------------------------------------------------- */

static size_t put_guid(char *buf, size_t n, size_t at, const GUID *g) {
    return rt_report_appendf(buf, n, at, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", (unsigned)g->Data1,
                             (unsigned)g->Data2, (unsigned)g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
                             g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

/* The name of an IID, when a registered interface or one of the bases has it. */
static const char *iid_name(const GUID *iid) {
    if (iid_none(iid)) return NULL;
    for (const RtComTable *t = tables; t; t = t->next)
        for (unsigned i = 0; i < t->nifaces; i++) {
            const RtComIface *it = t->ifaces[i];
            if (!memcmp(&it->iid, iid, sizeof *iid)) return it->name;
            for (unsigned k = 0; k < it->nchain; k++)
                if (!memcmp(&it->chain[k].iid, iid, sizeof *iid)) return it->chain[k].name;
        }
    return NULL;
}

HRESULT rt_com_QueryInterface(Ctx *ctx, void *This, REFIID riid, void **ppv) {
    RtComObject *o = this_face(ctx, This)->obj;
    if (!riid || !ppv) RT_UNIMPLEMENTED(ctx, "QueryInterface with riid %p and out-pointer %p", (const void *)riid, (void *)ppv);
    if (o->cls->refuses && o->cls->refuses(o->state, riid)) {      /* a documented "no" of this class */
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    RtComFace *found = NULL;
    /* IUnknown: the last base of faces[0]'s chain.  A class whose faces are root interfaces has no
     * IUnknown (its chain is empty, or ends in a root with no IID), so nothing is matched here. */
    const RtComIface *f0 = o->cls->faces[0];
    if (f0->nchain && !iid_none(&f0->chain[f0->nchain - 1].iid) && !memcmp(&f0->chain[f0->nchain - 1].iid, riid, sizeof *riid))
        found = &o->face[0];
    for (unsigned i = 0; !found && i < o->nfaces; i++)
        if (face_answers(o->cls->faces[i], riid)) found = &o->face[i];
    if (!found) {
        char what[512];
        size_t at = put_guid(what, sizeof what, 0, riid);
        const char *name = iid_name(riid);
        at = rt_report_appendf(what, sizeof what, at, " (%s)", name ? name : "an interface the runtime does not describe");
        at = rt_report_appendf(what, sizeof what, at, ": object #%llu of class %s presents only",
                               (unsigned long long)o->serial, o->cls->name);
        for (unsigned i = 0; i < o->nfaces; i++)
            at = rt_report_appendf(what, sizeof what, at, "%s %s", i ? "," : "", o->cls->faces[i]->name);
        RT_UNIMPLEMENTED(ctx, "QueryInterface for %s (and bases)", what);
    }
    add(o, ONE_REF);
    *ppv = found;
    return S_OK;
}

void  rt_com_accept(Ctx *ctx, void *This)  { (void)this_face(ctx, This); }
/* What the guest is told is the count of the application's references: the holds are left
 * out (rt/com.h, "holds").  So the last Release of the application returns 0 also while the
 * object lives on, held. */
ULONG rt_com_AddRef(Ctx *ctx, void *This) {
    uint64_t c = add(this_face(ctx, This)->obj, ONE_REF);
    return (ULONG)(total_of(c) - holds_of(c));
}

ULONG rt_com_Release(Ctx *ctx, void *This) {
    RtComObject *o = this_face(ctx, This)->obj;
    uint64_t c;
    if (!take(o, ONE_REF, &c))
        RT_UNIMPLEMENTED(ctx, "Release of object #%llu of class %s, to which the application holds no reference (the "
                         "runtime's own objects hold it %d time(s))", (unsigned long long)o->serial, o->cls->name,
                         (int)holds_of(atomic_load(&o->count)));
    return (ULONG)(total_of(c) - holds_of(c));
}

/* ---- counts ------------------------------------------------------------------------------------------ */

void rt_com_first_call(Ctx *ctx, const RtComMethod *m, RtComCount *c) {
    char where[160];
    uint64_t caller = LD64(ctx->r[4]);          /* entered like any function: rsp points at the return address */
    atomic_store(&c->first_thread, RT_THREAD(ctx)->id);
    atomic_store(&c->first_caller, caller);
    rt_log(RT_CH_COM, RT_LL_INFO, "first call of %s, from %s on thread 0x%x", m->entry.name,
           rt_module_describe(caller, where, sizeof where), RT_THREAD(ctx)->id);
}

_Atomic(RtComCount *) rt_com_watched;
static RtComWatchFn *_Atomic watch_fn;

void rt_com_watch(const RtComIface *iface, const char *method, RtComWatchFn *fn) {
    if (!fn) rt_fatal("rt_com_watch: no function");
    for (unsigned s = 0; s < iface->nmethods; s++)
        if (!strcmp(iface->methods[s].name, method)) {
            RtComWatchFn *none = NULL;
            if (!atomic_compare_exchange_strong(&watch_fn, &none, fn))
                rt_fatal("rt_com_watch: %s::%s: a method is watched already (one watch per process)", iface->name, method);
            atomic_store(&rt_com_watched, &iface->counts[s]);
            return;
        }
    rt_fatal("rt_com_watch: %s has no method %s", iface->name, method);
}

void rt_com_watch_call(Ctx *ctx, const RtComMethod *m, uint64_t nth) {
    RtComWatchFn *fn = atomic_load(&watch_fn);
    if (fn) fn(ctx, m, nth);
}

uint64_t rt_com_calls(const RtComIface *iface, const char *method) {
    for (unsigned s = 0; s < iface->nmethods; s++)
        if (!strcmp(iface->methods[s].name, method)) return atomic_load(&iface->counts[s].calls);
    return 0;
}

static const char *body_name(uint8_t body) {
    switch (body) {
    case RT_COM_BODY_ACCEPT:  return "accept";
    case RT_COM_BODY_UNKNOWN: return "common";
    case RT_COM_BODY_FILE:    return "body";
    default:                  return "stop";
    }
}

size_t rt_com_counts_format(char *buf, size_t n) {
    uint64_t total = 0;
    unsigned methods = 0, described = 0, ifaces = 0;
    if (n) buf[0] = 0;
    for (const RtComTable *t = tables; t; t = t->next)
        for (unsigned i = 0; i < t->nifaces; i++) {
            const RtComIface *it = t->ifaces[i];
            ifaces++;
            described += it->nmethods;
            for (unsigned s = 0; s < it->nmethods; s++) {
                uint64_t c = atomic_load(&it->counts[s].calls);
                total += c;
                methods += c != 0;
            }
        }
    if (!total) return 0;
    size_t at = rt_report_appendf(buf, n, 0, "com: calls through interface slots: %llu call%s of %u method%s (%u methods of "
                                  "%u interfaces are described)\n", (unsigned long long)total, total == 1 ? "" : "s",
                                  methods, methods == 1 ? "" : "s", described, ifaces);
    /* Tables in registration order, which is the list backwards: walk it by index. */
    unsigned ntables = 0;
    for (const RtComTable *t = tables; t; t = t->next) ntables++;
    for (unsigned k = ntables; k-- > 0;) {
        const RtComTable *t = tables;
        for (unsigned j = 0; j < k; j++) t = t->next;
        for (unsigned i = 0; i < t->nifaces; i++) {
            const RtComIface *it = t->ifaces[i];
            for (unsigned s = 0; s < it->nmethods; s++) {
                uint64_t c = atomic_load(&it->counts[s].calls);
                if (!c) continue;
                char where[160];
                rt_module_describe(atomic_load(&it->counts[s].first_caller), where, sizeof where);
                at = rt_report_appendf(buf, n, at, "com: %10llu  %s  first from %s, thread 0x%x  [%s]\n",
                                       (unsigned long long)c, it->methods[s].entry.name, where,
                                       (unsigned)atomic_load(&it->counts[s].first_thread), body_name(it->methods[s].body));
            }
        }
    }
    return at;
}

/* Eight: a driver has three of its own (the network, the stand-ins, the input script), the
 * do-nothing device its ledger and the seam behind it (unit pic-seam: the fifth one made every
 * run of the game fatal while the limit was four). */
#define COUNTS_ALSO 8
static void (*_Atomic counts_also[COUNTS_ALSO])(void);

void rt_com_counts_also(void (*fn)(void)) {
    for (unsigned i = 0; i < COUNTS_ALSO; i++) {
        void (*old)(void) = NULL;
        if (atomic_load(&counts_also[i]) == fn || atomic_compare_exchange_strong(&counts_also[i], &old, fn) || old == fn) return;
    }
    rt_fatal("rt_com_counts_also: more than %d functions", COUNTS_ALSO);
}

void rt_com_counts_print(void) {
    static _Atomic int printed;
    static char table[24576];           /* static: a fatal report may be on a small signal stack */
    if (atomic_exchange(&printed, 1)) return;
    size_t len = rt_com_counts_format(table, sizeof table);
    if (len) rt_log_raw(table, len);
    for (unsigned i = 0; i < COUNTS_ALSO; i++) {
        void (*fn)(void) = atomic_load(&counts_also[i]);
        if (fn) fn();
    }
}

/* ---- the stop of a method with no body ----------------------------------------------------------------- */

static uint64_t arg_bits(Ctx *ctx, uint8_t cls, unsigned i) {
    static const uint8_t reg_of[4] = { 1, 2, 8, 9 };
    if (i < 4 && (cls == RT_AC_F32 || cls == RT_AC_F64)) return ctx->xmm[i].q[0];
    if (i < 4) return ctx->r[reg_of[i]];
    return LD64(ctx->r[4] + 0x28 + 8 * (uint64_t)(i - 4));
}

/* True if [addr, addr + len) can be read now.  Guest memory is the two arenas
 * (rt_report_readable) and also the heaps, which are host malloc zones outside them
 * (core/heap.c): for an address outside the arenas the kernel is asked.  Only for the stop's
 * report, which is not a signal handler. */
static bool readable(uint64_t addr, uint64_t len) {
    if (rt_report_readable(addr, len)) return true;
    if (rt_vm_in_vm_arena(addr) || rt_module_from_address(addr)) return false;
    for (uint64_t at = addr; at < addr + len;) {
        mach_vm_address_t a = at;
        mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        if (mach_vm_region(mach_task_self(), &a, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count,
                           &object) != KERN_SUCCESS) return false;
        if (a > at || !(info.protection & VM_PROT_READ)) return false;
        at = a + size;
    }
    return true;
}

static size_t put_arg(char *buf, size_t n, size_t at, const char *type, uint8_t cls, uint64_t v) {
    switch (cls) {
    case RT_AC_I8: case RT_AC_U8:   return rt_report_appendf(buf, n, at, "%02x", (unsigned)(uint8_t)v);
    case RT_AC_I16: case RT_AC_U16: return rt_report_appendf(buf, n, at, "%04x", (unsigned)(uint16_t)v);
    case RT_AC_I32: case RT_AC_U32: return rt_report_appendf(buf, n, at, "%08x (%u)", (unsigned)(uint32_t)v, (unsigned)(uint32_t)v);
    case RT_AC_I64: case RT_AC_U64: return rt_report_appendf(buf, n, at, "%016llx", (unsigned long long)v);
    case RT_AC_F32: { float f; uint32_t b = (uint32_t)v; memcpy(&f, &b, 4);
                      return rt_report_appendf(buf, n, at, "%g (bits %08x)", (double)f, (unsigned)b); }
    case RT_AC_F64: { double d; memcpy(&d, &v, 8);
                      return rt_report_appendf(buf, n, at, "%g (bits %016llx)", d, (unsigned long long)v); }
    default: break;
    }
    at = rt_report_appendf(buf, n, at, "%016llx", (unsigned long long)v);
    if (v < 0x10000 || cls == RT_AC_HANDLE || cls == RT_AC_GUESTFN) return at;
    if (!strcmp(type, "REFIID") || !strcmp(type, "REFGUID")) {
        if (!readable(v, 16)) return rt_report_appendf(buf, n, at, " (not readable)");
        const GUID *g = (const GUID *)(uintptr_t)v;
        const char *name = iid_name(g);
        at = rt_report_appendf(buf, n, at, " -> ");
        at = put_guid(buf, n, at, g);
        return name ? rt_report_appendf(buf, n, at, " %s", name) : at;
    }
    if (!readable(v, 32)) return rt_report_appendf(buf, n, at, " (not readable memory)");
    if (cls == RT_AC_STR) {
        const unsigned char *s = (const unsigned char *)(uintptr_t)v;
        at = rt_report_appendf(buf, n, at, " \"");
        for (unsigned i = 0; i < 32 && s[i] >= 0x20 && s[i] < 0x7f; i++) at = rt_report_appendf(buf, n, at, "%c", s[i]);
        return rt_report_appendf(buf, n, at, "\"");
    }
    const RtComFace *f = as_face((const void *)(uintptr_t)v);
    if (f) {
        const RtComObject *o = f->obj;
        if (atomic_load(&((RtComFace *)(uintptr_t)v)->magic) != FACE_LIVE)
            return rt_report_appendf(buf, n, at, " -> object #%llu of class %s, DESTROYED", (unsigned long long)o->serial, o->cls->name);
        uint64_t c = atomic_load(&((RtComObject *)(uintptr_t)o)->count);
        at = rt_report_appendf(buf, n, at, " -> object #%llu of class %s (its %s), %d reference%s",
                               (unsigned long long)o->serial, o->cls->name, o->cls->faces[f->index]->name,
                               (int)(total_of(c) - holds_of(c)), total_of(c) - holds_of(c) == 1 ? "" : "s");
        return holds_of(c) ? rt_report_appendf(buf, n, at, " and %d hold%s", (int)holds_of(c), holds_of(c) == 1 ? "" : "s") : at;
    }
    at = rt_report_appendf(buf, n, at, " ->");
    for (unsigned k = 0; k < 8; k++) at = rt_report_appendf(buf, n, at, " %08x", (unsigned)LD32(v + 4 * k));
    return at;
}

_Noreturn void rt_com_stop(Ctx *ctx) {
    char buf[8192];
    const RtShimFrame *fr = RT_THREAD(ctx)->shim;
    const RtComMethod *m = (const RtComMethod *)fr->shim;          /* entry is the first member */
    const RtShimEntry *e = &m->entry;
    size_t at = rt_report_appendf(buf, sizeof buf, 0, "FATAL unimplemented interface method %s\n", e->name);
    at = rt_report_appendf(buf, sizeof buf, at, "  %s.dll, vtable slot %u of %s, declared by %s (%s)\n  %s\n", e->lib,
                           (unsigned)m->slot, m->iface->name, m->declared_by, m->iface->header, m->where);
    at = rt_report_appendf(buf, sizeof buf, at, "  %s%s%s(\n", m->ret_type, m->ret_type[strlen(m->ret_type) - 1] == '*' ? "" : " ", m->name);
    for (unsigned i = 0; i < e->nargs; i++) {
        const char *t = m->arg_types[i];
        at = rt_report_appendf(buf, sizeof buf, at, "    %s%s%s = ", t, t[strlen(t) - 1] == '*' ? "" : " ", m->arg_names[i]);
        at = put_arg(buf, sizeof buf, at, t, e->args[i], arg_bits(ctx, e->args[i], i));
        at = rt_report_appendf(buf, sizeof buf, at, "\n");
    }
    at = rt_report_appendf(buf, sizeof buf, at, "  )\n");
    rt_report_context(buf + at, sizeof buf - at, ctx, fr->entry_rsp);
    rt_report_die(buf);                                             /* the count table follows (rt_report_abort) */
}
