/* The interface layer: guest-visible objects with vtables (docs/RUNTIME.md section 3.7;
 * docs/handoff/d3d-interface-layer.md, section 2, is the page for whoever writes one method).
 *
 * To lifted code an interface pointer is the address of memory whose first word is the
 * address of a table of function addresses; it calls slot s as [[this] + 8*s] with `this` in
 * rcx.  This layer is what stands behind such a pointer when the object is ours.
 *
 * WHAT AN OBJECT IS (this header, runtime/core/com.c, the generated tables):
 *   description   one RtComIface per interface, generated from runtime/com/desc/<name>.idef:
 *                 IID, chain of bases, every vtable slot with its prototype.
 *   vtable        one per interface, in the slot of the native module the description names
 *                 (rt/module.h, +0x80000): 8 bytes per slot, each the address of a stub of
 *                 that module, and the dispatch table maps the stub to the method's thunk.
 *                 Made once, in rt_init, before the dispatch table is sealed.
 *   class         an RtComClass, written by whoever provides objects: a name, the interfaces
 *                 an object of it presents (one FACE each), the size of its state.
 *   object        rt_com_new(class): one block of guest memory with a reference count, the
 *                 faces and the state.  A face is what the guest holds: its first word is the
 *                 vtable address.  QueryInterface hands out the object's other faces.
 *   counts        calls per (interface, method), the first caller of each (below).
 *
 * WHAT A METHOD DOES is not here.  The generator (runtime/tools/gen_com.py) writes one thunk
 * per (interface, slot); the thunk counts the call, fetches the arguments by the described
 * prototype and then does one of four things, decided when the tables are generated:
 *   RT_COM_BODY_UNKNOWN  QueryInterface, AddRef, Release: the three functions below, for all
 *   RT_COM_BODY_FILE     calls the typed function of a body file, <bodies>/<Interface>/<Method>.c
 *   RT_COM_BODY_ACCEPT   returns S_OK (or nothing): the description's accept list marks it
 *   RT_COM_BODY_STOP     everything else: a fatal report that names interface and method, the
 *                        caller and every argument (rt_com_stop)
 * At Gate 4 the same thunks call proxies instead of body files; nothing in this header
 * changes for that.
 *
 * A method's thunk is a shim thunk: it enters with RT_SHIM_ENTER on an RtShimEntry of its own
 * (library = the owner module, name = "Interface::Method"), so the call trace, RT_UNIMPLEMENTED
 * in a body, the fatal reports and the unwinder treat a method exactly like an import.
 *
 * COST of a call through a slot: the dispatch-table lookup every indirect call of lifted code
 * makes, then the thunk: what a shim's thunk does plus one atomic increment.  Measured in
 * runtime/tests/core/com_cost.c (docs/handoff/d3d-interface-layer.md has the numbers).
 *
 * THREADS.  Reference counts and call counts are atomic; objects may be created, used and
 * released on any guest thread.  Registration (rt_com_register) is rt_init's, single-threaded.
 */
#ifndef RT_COM_H
#define RT_COM_H

#include <stdatomic.h>

#include "rt/shim.h"
#include "rt/win/com.h"

/* ---- descriptions (generated: build/gen/com_table.c) ----------------------------------------- */

typedef struct RtComAncestor {
    const char *name;               /* "ID3D11DeviceChild" */
    GUID        iid;
    uint16_t    nslots;             /* vtable slots the base has: slots below this are its methods */
} RtComAncestor;

typedef enum { RT_COM_BODY_STOP = 0, RT_COM_BODY_ACCEPT, RT_COM_BODY_UNKNOWN, RT_COM_BODY_FILE } RtComBody;

struct RtComIface;

typedef struct RtComMethod {
    RtShimEntry        entry;       /* must stay first: a shim frame points at it.  lib = the owner as
                                       a C identifier, name = "ID3D11Device::CreateBuffer", nargs counts
                                       `This`, args[0] is RT_AC_PTR */
    const struct RtComIface *iface;
    const char        *name;        /* "CreateBuffer" */
    const char        *declared_by; /* the interface of the chain that introduces the slot */
    const char        *ret_type;    /* "HRESULT" */
    const char *const *arg_types;   /* entry.nargs strings as described; [0] is "ID3D11Device *" */
    const char *const *arg_names;   /* [0] is "This" */
    const char        *where;       /* a sentence for reports: which file is the body, which line of
                                       the accept list marks it, or which file a body would be */
    uint16_t           slot;
    uint8_t            body;        /* RtComBody */
} RtComMethod;

/* One per method, beside it.  `calls` counts every arrival in the thunk, whatever the body. */
typedef struct RtComCount {
    _Atomic uint64_t calls;
    _Atomic uint64_t first_caller;  /* return address of the first call: a guest code address */
    _Atomic uint32_t first_thread;  /* its guest thread id */
} RtComCount;

typedef struct RtComIface {
    const char          *name;      /* "ID3D11Device" */
    GUID                 iid;       /* all zero when the interface has none: a root interface (no base) */
    const char          *owner;     /* "d3d11.dll": the native module that holds vtable and stubs */
    const char          *header;    /* where the description was read from: "d3d11.h" */
    const RtComAncestor *chain;     /* bases, nearest first, IUnknown last */
    unsigned             nchain;
    const RtComMethod   *methods;   /* nmethods, in slot order (inherited ones first) */
    RtComCount          *counts;    /* nmethods, same order */
    unsigned             nmethods;
    uint64_t             vtable;    /* guest address of the vtable; 0 until rt_com_register */
} RtComIface;

/* A set of descriptions: what one run of the generator emitted. */
typedef struct RtComTable {
    RtComIface *const  *ifaces;
    unsigned            nifaces;
    const char         *what;       /* for the count table's heading: "runtime/com/desc" */
    struct RtComTable  *next;       /* the layer's list; leave NULL */
} RtComTable;

extern RtComTable rt_com_table;     /* the runtime's own set: the interfaces of runtime/com/desc */

/* rt_init calls this after the native modules exist and before the dispatch table is sealed:
 * rt_com_register(&rt_com_table). */
void rt_com_init(void);

/* Give every interface of `t` its vtable and every method its stub (rt_native_stub in the
 * owner's slot, named "Interface::Method", so rt_module_describe prints
 * "d3d11.dll!ID3D11Device::CreateBuffer").  Only before the dispatch table is sealed
 * (rt_com_init, or RtConfig.before_seal for a table of the embedding program).  Fatal: after
 * the seal; an owner that is not a native module of the manifest; an interface name or IID
 * that is already registered; a table registered twice. */
void rt_com_register(RtComTable *t);

/* The registered interface with this IID or name, or NULL. */
const RtComIface *rt_com_iface_by_iid(const GUID *iid);
const RtComIface *rt_com_iface_by_name(const char *name);

/* ---- classes and objects ------------------------------------------------------------------------ */

typedef struct RtComClass {
    const char *name;                       /* for reports: "D3D11Device" */
    RtComIface *const *faces;               /* the interfaces an object presents, one face each.  A face
                                               also answers for every base of its interface.  faces[0]
                                               is the object's identity: what QueryInterface returns
                                               for IUnknown */
    unsigned    nfaces;                     /* at least 1 */
    uint32_t    state_size;                 /* bytes of state per object, zeroed at creation; may be 0 */
    void      (*destroy)(void *state);      /* at the last Release, before the object is dead; may be NULL.
                                               Releases what the state holds (other objects, memory) */
    bool      (*refuses)(const void *state, const GUID *iid);
                                            /* may be NULL.  QueryInterface asks it first: true makes the
                                               answer E_NOINTERFACE with *ppv = NULL and no reference, for
                                               an IID the class is DOCUMENTED to refuse (a buffer asked for
                                               ID3D11Texture2D).  `state` is the object's (NULL when the
                                               class has none).  It is never a default: an IID the class
                                               neither presents nor refuses stays a stop */
} RtComClass;

/* A new object of `cls` with one reference.  Returns face 0, the pointer to hand to the guest.
 * Fatal: a class with no face, with two faces that answer for the same interface, or with an
 * interface that is not registered; memory exhausted (rt_galloc). */
void *rt_com_new(const RtComClass *cls);

/* The face of the same object that answers for `iface` (it presents `iface` or an interface
 * derived from it), or NULL.  Does not change the reference count.  `any` is any face of a
 * live object; fatal otherwise. */
void *rt_com_face(void *any, const RtComIface *iface);

/* The state of the object `This` is a face of.  Stops (rt_unimplemented, naming the method
 * the thread is in) unless `This` is a face of a live object of class `cls`: a method handed
 * a foreign pointer, an object of another class or one already destroyed never goes on. */
void *rt_com_state(Ctx *ctx, void *This, const RtComClass *cls);

/* The same for host code that is not inside a method (a constructor of the object model, a
 * destroy function, a test): fatal unless `any` is a face of a live object of class `cls`. */
void *rt_com_state_of(void *any, const RtComClass *cls);

/* The class of the live object `p` is a face of, or NULL: not a face of this layer (a guest
 * object, garbage that happens to be readable) or an object that was destroyed.  `p` must
 * be readable for 32 bytes or NULL. */
const RtComClass *rt_com_class_of(const void *p);

/* REFERENCES AND HOLDS.  An object lives while it has a reference.  A reference is either
 * the application's (the guest's AddRef, a creator or getter handing the object out,
 * rt_com_addref) or a HOLD: one the runtime's own objects keep on each other (rt_com_hold: a
 * view on its resource, the context on what is bound, a swap chain on its back buffer).  The
 * guest never sees a hold: its AddRef and Release return the count of the application's
 * references alone, as Windows does (Wine, dlls/d3d11/tests/d3d11.c: a texture's count is the
 * same before and after a view of it is made, test_create_rendertarget_view; a swap chain's
 * buffer has count 1 after GetBuffer and still 1 with two views on it and one of them bound,
 * test_swapchain_views; read by docs/reviews/d3d-contract-check-2.md, F3).  So the
 * application's last Release returns 0 while a held object lives on, and a later AddRef, or a
 * getter that hands it out again, finds it alive.
 *
 * rt_com_refs: every reference, holds included: what keeps the object alive (all faces share
 * one count).  rt_com_public_refs: the application's alone, what its AddRef would return less
 * one.  rt_com_serial: the creation number (objects are numbered from 1 in the order
 * rt_com_new made them, for reports).  Fatal unless `any` is a face of a live object. */
LONG     rt_com_refs(void *any);
LONG     rt_com_public_refs(void *any);
uint64_t rt_com_serial(void *any);

/* Host-side AddRef and Release, for a body that hands out or takes back a reference of the
 * application's (a getter that returns a bound object adds one; a creator that only validated
 * releases what it made), and for the references of a child on its parent, which Windows
 * shows (a device's count rises with every object it makes).  They return the new count of
 * ALL references (rt_com_refs) and are not counted as calls.  rt_com_release is fatal when
 * only holds are left. */
ULONG rt_com_addref(void *any);
ULONG rt_com_release(void *any);

/* A reference for a caller that found the object through a pointer nobody holds a reference
 * for (a lookup table a destroy function removes the object from): true and one more
 * reference, or false when the object's last reference is already gone and its destroy is
 * running or about to run on another thread; the caller must then not use the object.  Between
 * the count reaching 0 and the faces being marked dead the object still looks alive
 * (rt_com_class_of answers), which is why rt_com_addref must never be used for this.  The
 * caller must keep the object from being marked dead while it asks: hold the lock the
 * destroy function takes to remove it. */
bool rt_com_addref_live(void *any);

/* A hold and its end, for a record that keeps a pointer to another object without the guest
 * seeing it.  They return the new count of all references.  The object is destroyed when its
 * last reference of either kind goes.  rt_com_unhold on an object with no hold releases a
 * plain reference instead and logs a `com` warning (a record that was filled by hand with
 * rt_com_addref). */
ULONG rt_com_hold(void *any);
ULONG rt_com_unhold(void *any);

/* ---- the three methods every interface starts with (the generated thunks call these) ------------ */

/* *ppv = the face that answers for *riid, with one more reference; S_OK.  IUnknown's IID
 * gives face 0 whichever face was asked.  An IID the class's `refuses` names: E_NOINTERFACE
 * and *ppv = NULL (IUnknown::QueryInterface's page: "If the object doesn't support the
 * interface, the method sets *ppvObject to nullptr"; from memory, unit d3d-contract).  Any
 * other IID no face answers for is a STOP that prints the IID, its name when a registered
 * interface (or a base of one) has it, and the class with its faces: the layer never answers
 * E_NOINTERFACE on its own.  Also a stop: riid or ppv NULL. */
HRESULT rt_com_QueryInterface(Ctx *ctx, void *This, REFIID riid, void **ppv);
ULONG   rt_com_AddRef(Ctx *ctx, void *This);
/* Both return the count of the application's references (holds are not reported: above).
 * When the last reference of either kind goes, the class's destroy runs, then the object is
 * DEAD: its memory is kept and every later call through one of its faces stops, naming the
 * class and the creation number.  (Nothing is handed back to the heap, so a stale pointer can
 * never reach a newer object.)  A Release when the application has no reference left on an
 * object that lives on held is a stop. */
ULONG   rt_com_Release(Ctx *ctx, void *This);

/* ---- for the generated thunks ----------------------------------------------------------------- */

/* The stop of a method with no body: "FATAL unimplemented interface method I::M", where a
 * body would be written, the prototype with each argument's value (integers at their width,
 * the 32 bytes behind a pointer to readable guest memory, the GUID and its interface name
 * behind a REFIID, class and creation number behind a pointer to one of our objects), then
 * the caller and the guest stack as every report has them, then the count table. */
_Noreturn void rt_com_stop(Ctx *ctx);

/* The body of a method marked accept: stops unless `This` is a face of a live object (as
 * this_face does for the three above), and does nothing else. */
void rt_com_accept(Ctx *ctx, void *This);

void rt_com_first_call(Ctx *ctx, const RtComMethod *m, RtComCount *c);

/* A WATCH: a driver's own function at one method (rt_com_watch).  The count record of the
 * watched method, or NULL: the thunk compares its own with it, which is all a method that is
 * not watched pays. */
extern _Atomic(RtComCount *) rt_com_watched;
void rt_com_watch_call(Ctx *ctx, const RtComMethod *m, uint64_t nth);

TF_INLINE void rt_com_enter(Ctx *ctx, const RtComMethod *m, RtComCount *c, RtShimFrame *f) {
    rt_shim_enter(ctx, &m->entry, f);
    const uint64_t before = atomic_fetch_add_explicit(&c->calls, 1, memory_order_relaxed);
    if (__builtin_expect(before == 0, 0))
        rt_com_first_call(ctx, m, c);           /* one `com` info line, with the caller */
    if (__builtin_expect(atomic_load_explicit(&rt_com_watched, memory_order_relaxed) == c, 0))
        rt_com_watch_call(ctx, m, before + 1);
}
#define RT_COM_ENTER(ctx, m, c)  RtShimFrame rt_frame_; rt_com_enter((ctx), (m), (c), &rt_frame_)

/* ---- the count table ---------------------------------------------------------------------------- */

/* Calls of `method` of `iface` so far (0 for a name the interface does not have). */
uint64_t rt_com_calls(const RtComIface *iface, const char *method);

/* The table as text: a heading with the totals, then one line per method that was called, in
 * description order (interface, slot):
 *     com:      <calls>  <Interface>::<Method>  first from <module+rva>, thread 0x<id>  [<body>]
 * <body> is stop, accept, common or body.  Nothing at all when no method was ever called.
 * Never writes past buf[n-1]; returns the length.  Takes no lock and allocates nothing: it
 * may run inside a fatal report and in a signal handler. */
size_t rt_com_counts_format(char *buf, size_t n);

/* Write the table with rt_log_raw, once per process (a second call does nothing).  Called
 * at the end of every fatal report (rt_report_abort), by the exit sequence of ExitProcess
 * and TerminateProcess, and by a driver whose run ends by returning. */
void rt_com_counts_print(void);

/* A driver's watch on one method.  `fn` runs in the thunk of `method` of `iface` on the calling
 * guest thread, after the call was counted (and its first call logged) and before the body:
 * `nth` is the number of this call, 1 for the first; calls on several threads may arrive in
 * any order and at once.  The guest sees nothing of it: `fn` must not change the context, and
 * what the method does and returns is untouched (unless `fn` ends the process, which is what
 * gate3's GATE3_PRESENTS does at the swap chain's Present).  One watch per process: a second
 * rt_com_watch, or a method the interface does not have, is fatal.  Methods of the layer's
 * own (QueryInterface, AddRef, Release) can be watched like any other. */
typedef void RtComWatchFn(Ctx *ctx, const RtComMethod *m, uint64_t nth);
void rt_com_watch(const RtComIface *iface, const char *method, RtComWatchFn *fn);

/* Have `fn` called after the table whenever rt_com_counts_print writes it (up to 8 functions;
 * naming one twice is one).  For whoever provides objects and keeps totals of its own beside
 * the call counts (the do-nothing device's ledger).  `fn` writes with rt_log_raw, takes no
 * lock and allocates nothing: it may run inside a fatal report. */
void rt_com_counts_also(void (*fn)(void));

/* ---- writing a method body ---------------------------------------------------------------------- */

/* The typed function of one method, in <bodies>/<Interface>/<Method>.c; `ctx` and `This` are
 * in scope.  ONE line, starting in column 1, with the parameters exactly as the description
 * has them (the generator compares):
 *
 *     COM_METHOD(ID3D11Device, D3D_FEATURE_LEVEL, GetFeatureLevel)
 *     COM_METHOD(ID3D11Device, HRESULT, CreateBuffer, const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Buffer **ppBuffer)
 *
 * <Interface> is one of the described interfaces, or a base of described interfaces (then
 * the one body serves the slot in every described interface that inherits it and has no
 * body of its own). */
#define COM_METHOD(iface, ret, name, ...) \
    ret impl_##iface##_##name(Ctx *ctx __attribute__((unused)), iface *This __attribute__((unused)) __VA_OPT__(,) __VA_ARGS__)

#endif /* RT_COM_H */
