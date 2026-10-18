/* The loader: LoadLibrary, FreeLibrary, entry points, thread attach and detach calls
 * (docs/RUNTIME.md sections 1.5 and 1.6).  Specification: rt_module_load, rt_module_load_a,
 * rt_module_free, rt_module_disable_thread_calls, rt_loader_thread_attach,
 * rt_loader_thread_detach in rt/module.h, and rt_ldr_call_entry / rt_ldr_order_* in loader.h.
 * What it is built from: docs/handoff/rt-loader-pieces.md; what it leaves:
 * docs/handoff/rt-loader-load.md.
 *
 * State, guarded by the loader lock: the load order, every RtLdrModule.state,
 * RtLdrModule.no_thread_calls, RtModule.loaded and RtModule.load_count.  Every public
 * function here takes the loader lock for the length of the call and runs guest code (entry
 * points, TLS callbacks) under it.  The lock is never dropped in the middle of a load, so
 * another thread sees a module UNLOADED, LOADED or FAILED and never LOADING: LOADING seen by
 * a load always means "higher up this thread's own recursion".
 *
 * DECISIONS where rt/module.h was silent or wrong (each is also in the handoff).
 *   - The order of the argument checks: ctx (fatal), a flag bit nobody defines
 *     (ERROR_INVALID_PARAMETER), a NULL or empty name (ERROR_INVALID_PARAMETER), a flag we do
 *     not handle (the unimplemented report), then the look-up.  So LOAD_LIBRARY_AS_DATAFILE
 *     with a NULL name fails and does not stop, and with a name no module has it stops.
 *   - The W form looks up the LAST PATH COMPONENT with rt_module_find_w, not the whole string:
 *     that function refuses a character outside ASCII anywhere, and a directory is ignored
 *     (section 1.5), so a directory with such a character must not hide the module.
 *   - A successful load adds 1 to load_count; rt/module.h said "load_count = 1", which is the
 *     same thing except when the entry point loaded its own module (then it is 2, as the
 *     plan's case 14 expects and as Windows counts).
 *   - A module that FAILS has load_count 0, whatever an import cycle or its own entry point
 *     added on the way.
 *   - An import that is ABSENT fails the importer with ERROR_MOD_NOT_FOUND the first time and,
 *     because the importer is then FAILED, with ERROR_DLL_INIT_FAILED every time after.
 *   - The thread calls go to the modules that were in the load order when the call began: a
 *     module that an entry point loads during DLL_THREAD_ATTACH got DLL_PROCESS_ATTACH on
 *     that thread and gets no DLL_THREAD_ATTACH for it.
 *   - lpReserved is 0 in every call.  Windows passes a non-NULL value to DLL_PROCESS_ATTACH
 *     of a module that is loaded as another module's static import; here every load is a
 *     dynamic one to the entry point.
 *   - A thread that ends inside an entry point or TLS callback (ExitThread there) is a stop,
 *     not a state to recover from: rt_thread_exit (core/thread_run.c) asks rt_loader_in_entry
 *     before it unwinds, and rt_loader_thread_detach asks again for every other way a thread
 *     can end.  The unwind would leave the loader lock held for ever.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader.h"

static RtLdrModule **order;             /* lifted modules from step 5 of their load on, oldest first;
                                           a module whose entry point returns FALSE leaves it */
static unsigned      norder;

#define IGNORED_FLAGS (LOAD_WITH_ALTERED_SEARCH_PATH | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR \
                       | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS \
                       | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)

static const struct { DWORD bit; const char *name; } unhandled_flags[] = {
    { DONT_RESOLVE_DLL_REFERENCES,        "DONT_RESOLVE_DLL_REFERENCES" },
    { LOAD_LIBRARY_AS_DATAFILE,           "LOAD_LIBRARY_AS_DATAFILE" },
    { LOAD_IGNORE_CODE_AUTHZ_LEVEL,       "LOAD_IGNORE_CODE_AUTHZ_LEVEL" },
    { LOAD_LIBRARY_AS_IMAGE_RESOURCE,     "LOAD_LIBRARY_AS_IMAGE_RESOURCE" },
    { LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE, "LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE" },
};
#define NUNHANDLED (sizeof unhandled_flags / sizeof unhandled_flags[0])

/* ---- the load order -------------------------------------------------------------------------- */

unsigned rt_ldr_order_count(void) { return norder; }

RtLdrModule *rt_ldr_order_at(unsigned i) {
    if (i >= norder) rt_fatal("rt_ldr_order_at(%u): the load order has %u module%s", i, norder, norder == 1 ? "" : "s");
    return order[i];
}

static void order_append(RtLdrModule *lm) {
    RtLdrModule **grown = realloc(order, (norder + 1) * sizeof *order);
    if (!grown) rt_fatal("out of memory");
    order = grown;
    lm->order = norder;
    order[norder++] = lm;
}

/* Not always the last one: its entry point may have loaded other modules after it. */
static void order_remove(RtLdrModule *lm) {
    unsigned i = lm->order;
    if (i >= norder || order[i] != lm) rt_fatal("%s is not in the load order at position %u", lm->m->name, i);
    for (; i + 1 < norder; i++) {
        order[i] = order[i + 1];
        order[i]->order = i;
    }
    norder--;
    lm->order = 0;
}

/* ---- entry points ---------------------------------------------------------------------------- */

BOOL rt_ldr_call_entry(Ctx *ctx, RtLdrModule *lm, DWORD reason) {
    RtModule *m = lm->m;
    if (!lm->img.mapped) rt_fatal("rt_ldr_call_entry(%s): the image is not built", m->name);
    if (!lm->img.entry_rva) return TRUE;
    rt_ldr_tls_thread(ctx);                             /* the mark needs the record */
    const uint64_t args[3] = { m->base, reason, 0 };
    uint64_t outer = rt_ldr_entry_enter(ctx, lm, reason);
    uint64_t rax = rt_guest_call(ctx, m->base + lm->img.entry_rva, 3, args);
    rt_ldr_entry_leave(ctx, outer);
    return (uint32_t)rax != 0;                          /* a BOOL is eax; the upper half of rax is garbage */
}

/* ---- loading --------------------------------------------------------------------------------- */

/* Load lifted module lm by the rules of rt_module_load, under the loader lock, on a recorded
 * thread.  Returns its module, or NULL with *err the Win32 error.  `asked` is the name the
 * caller of LoadLibrary gave, or NULL when the module is loaded as another module's import. */
static RtModule *load_lifted(Ctx *ctx, RtLdrModule *lm, const char *asked, DWORD *err) {
    RtModule *m = lm->m;
    char who[160];
    if (asked) snprintf(who, sizeof who, "LoadLibrary(\"%s\")", asked);
    else       snprintf(who, sizeof who, "import of %s", m->name);

    switch (lm->state) {
    case RT_LDR_ABSENT:
        rt_log(RT_CH_LOADER, RT_LL_WARN, "%s: %s has no DLL file in this run (ERROR_MOD_NOT_FOUND)", who, m->name);
        *err = ERROR_MOD_NOT_FOUND;
        return NULL;
    case RT_LDR_FAILED:
        rt_log(RT_CH_LOADER, RT_LL_WARN, "%s: an earlier load of %s failed; its entry point is not run a second "
               "time (ERROR_DLL_INIT_FAILED)", who, m->name);
        *err = ERROR_DLL_INIT_FAILED;
        return NULL;
    case RT_LDR_LOADING:                                /* higher up this thread's recursion */
    case RT_LDR_LOADED:
        m->load_count++;
        return m;
    case RT_LDR_UNLOADED:
        break;
    }

    /* 1. The image. */
    rt_ldr_image_map(lm);
    lm->state = RT_LDR_LOADING;
    m->loaded = true;

    /* 2. The lifted modules it imports from, in descriptor order, then its slots. */
    unsigned ndeps = rt_ldr_import_modules(lm, NULL, 0);
    if (ndeps) {
        RtModule **deps = calloc(ndeps, sizeof *deps);
        if (!deps) rt_fatal("out of memory");
        rt_ldr_import_modules(lm, deps, ndeps);
        for (unsigned i = 0; i < ndeps; i++) {
            if (deps[i]->native) continue;
            if (load_lifted(ctx, rt_ldr_module(deps[i]), NULL, err)) continue;
            rt_log(RT_CH_LOADER, RT_LL_ERROR, "%s: not loaded: %s, which it imports from, could not be loaded "
                   "(Win32 error %u); the module stays mapped and is marked failed", m->name, deps[i]->name, *err);
            free(deps);
            lm->state = RT_LDR_FAILED;
            m->load_count = 0;
            return NULL;
        }
        free(deps);
    }
    rt_ldr_bind(lm);

    /* 3 and 4. */
    if (lm->tls_index >= 0) rt_ldr_tls_load(lm);
    rt_ldr_image_protect(lm);

    /* 5. Its code runs for the first time. */
    order_append(lm);
    rt_ldr_tls_callbacks(ctx, lm, DLL_PROCESS_ATTACH);
    BOOL ok = rt_ldr_call_entry(ctx, lm, DLL_PROCESS_ATTACH);

    /* 6. */
    if (!ok) {
        order_remove(lm);
        lm->state = RT_LDR_FAILED;
        m->load_count = 0;
        rt_log(RT_CH_LOADER, RT_LL_ERROR, "%s: its entry point returned FALSE for DLL_PROCESS_ATTACH; the module "
               "stays mapped and is marked failed (ERROR_DLL_INIT_FAILED)", m->name);
        *err = ERROR_DLL_INIT_FAILED;
        return NULL;
    }
    lm->state = RT_LDR_LOADED;
    m->load_count++;
    rt_log(RT_CH_LOADER, RT_LL_INFO, "%s: loaded at %#llx, number %u in the load order%s", m->name,
           (unsigned long long)m->base, lm->order + 1, lm->img.entry_rva ? "" : " (it has no entry point)");
    return m;
}

/* The checks that come before the look-up.  `shown` is the name for messages ("" for NULL). */
static bool load_args_ok(Ctx *ctx, const char *fn, bool has_name, const char *shown, DWORD flags) {
    if (!ctx || !RT_THREAD(ctx) || RT_THREAD(ctx) != rt_thread_self())
        rt_fatal("%s: the ctx is not the calling guest thread's", fn);
    DWORD known = IGNORED_FLAGS;
    for (unsigned i = 0; i < NUNHANDLED; i++) known |= unhandled_flags[i].bit;
    if ((flags & ~known) || !has_name) {
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return false;
    }
    for (unsigned i = 0; i < NUNHANDLED; i++)
        if (flags & unhandled_flags[i].bit)
            RT_UNIMPLEMENTED(ctx, "LoadLibraryEx(\"%s\") with %s (flags %#x): only loading a module to run it is built",
                             shown, unhandled_flags[i].name, (unsigned)flags);
    return true;
}

/* `m` is what the registry has under the name asked for (NULL: nothing). */
static RtModule *load_found(Ctx *ctx, RtModule *m, const char *shown) {
    DWORD err = 0;
    rt_loader_lock();
    rt_ldr_tls_thread(ctx);                             /* guest thread 1 is recorded here, at its first load */
    if (!m) {
        rt_log(RT_CH_LOADER, RT_LL_WARN, "LoadLibrary(\"%s\"): the module registry has no such module "
               "(ERROR_MOD_NOT_FOUND)", shown);
        err = ERROR_MOD_NOT_FOUND;
    } else if (m->native) {
        m->load_count++;
    } else {
        m = load_lifted(ctx, rt_ldr_module(m), shown, &err);
    }
    rt_loader_unlock();
    if (err) {                                          /* after the entry points: they may have set their own */
        rt_thread_set_last_error(err);
        return NULL;
    }
    return m;
}

/* A name for a log line: printable ASCII as it is, anything else as '?'. */
static void show_char(char *out, size_t n, size_t i, unsigned c) {
    if (i + 1 < n) { out[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?'; out[i + 1] = 0; }
}

RtModule *rt_module_load(Ctx *ctx, const WCHAR *name, DWORD flags) {
    char shown[128] = "";
    const WCHAR *last = name;
    for (size_t i = 0; name && name[i]; i++) {
        show_char(shown, sizeof shown, i, name[i]);
        if (name[i] == '\\' || name[i] == '/') last = name + i + 1;
    }
    if (!load_args_ok(ctx, "rt_module_load", name && name[0], shown, flags)) return NULL;
    return load_found(ctx, rt_module_find_w(last), shown);
}

RtModule *rt_module_load_a(Ctx *ctx, const char *name, DWORD flags) {
    char shown[128] = "";
    for (size_t i = 0; name && name[i]; i++) show_char(shown, sizeof shown, i, (unsigned char)name[i]);
    if (!load_args_ok(ctx, "rt_module_load_a", name && name[0], shown, flags)) return NULL;
    return load_found(ctx, rt_module_find(name), shown);    /* a byte >= 0x80 in the last component: no module */
}

BOOL rt_module_free(RtModule *m) {
    if (!m) rt_fatal("rt_module_free: null module (the shim checks rt_module_from_handle first)");
    rt_loader_lock();
    if (m->load_count <= 0) {
        m->load_count = 0;
        rt_log(RT_CH_LOADER, RT_LL_WARN, "FreeLibrary(%s): freed more often than loaded", m->name);
    } else if (--m->load_count == 0) {
        rt_log(RT_CH_LOADER, RT_LL_INFO, "FreeLibrary(%s): its load count is 0; modules are never unloaded and no "
               "entry point is called", m->name);
    }
    rt_loader_unlock();
    return TRUE;
}

BOOL rt_module_disable_thread_calls(RtModule *m) {
    if (!m) rt_fatal("rt_module_disable_thread_calls: null module (the shim checks rt_module_from_handle first)");
    if (m->native) return TRUE;                         /* no entry point to call */
    rt_loader_lock();
    RtLdrModule *lm = rt_ldr_module(m);
    const bool has_tls = lm->tls_index >= 0;
    if (!has_tls) lm->no_thread_calls = true;
    rt_loader_unlock();
    if (has_tls) {
        rt_log(RT_CH_LOADER, RT_LL_WARN, "DisableThreadLibraryCalls(%s): refused, the module has static TLS; its "
               "entry point keeps being called for threads", m->name);
        rt_thread_set_last_error(ERROR_MOD_NOT_FOUND);
        return FALSE;
    }
    return TRUE;
}

/* ---- threads --------------------------------------------------------------------------------- */

/* The thread unit calls these two for every guest thread it starts and ends (core/thread_run.c,
 * core/thread_attach.c, rt_thread_finish in core/thread.c), and for guest thread 1 neither:
 * that one is recorded at its first rt_module_load.  The static TLS half (record the thread,
 * give and free its blocks) is loader_tls.c's.
 *
 * Both walk the modules that are in the load order when they begin.  An entry point may load
 * a module during the walk: it is appended, so the positions below `n` do not move (a module
 * that leaves the order because its entry point returned FALSE was appended after the walk
 * began too), and the array is read again at each step because it may have been reallocated. */
void rt_loader_thread_attach(Ctx *ctx) {
    rt_loader_lock();
    rt_ldr_tls_thread(ctx);                     /* records the thread, with a block per loaded TLS module */
    RtLdrThread *r = rt_ldr_thread_record(RT_THREAD(ctx));
    if (r->attached) rt_fatal("rt_loader_thread_attach: called twice for thread 0x%x", RT_THREAD(ctx)->id);
    r->attached = true;
    const unsigned n = norder;
    for (unsigned i = 0; i < n; i++) {
        RtLdrModule *lm = order[i];
        rt_ldr_tls_callbacks(ctx, lm, DLL_THREAD_ATTACH);
        if (!lm->no_thread_calls) (void)rt_ldr_call_entry(ctx, lm, DLL_THREAD_ATTACH);
    }
    rt_loader_unlock();
}

void rt_loader_thread_detach(Ctx *ctx) {
    rt_loader_lock();
    RtThread *t = ctx ? RT_THREAD(ctx) : NULL;
    RtLdrThread *r = t && t == rt_thread_self() ? rt_ldr_thread_record(t) : NULL;
    if (!r || !r->attached)
        rt_fatal("rt_loader_thread_detach: the calling thread was never attached (rt_loader_thread_attach)");
    /* A mark that is still set means the guest call that set it never returned: the thread
     * ended inside it, and the host frames that held the loader lock are gone with their
     * unlocks.  rt_thread_exit stops earlier, with the caller in the report; this is the
     * stop for every other way to get here (rt_thread_detach from inside a guest call). */
    const RtModule *in = NULL;
    DWORD reason = 0;
    if (rt_loader_in_entry(t, &in, &reason))
        rt_fatal("thread 0x%x ends inside the entry point or a TLS callback of %s (reason %u): that is not "
                 "supported, the loader lock taken for the call would stay held", t->id, in->name, reason);
    const unsigned n = norder;
    for (unsigned i = n; i-- > 0;) {
        RtLdrModule *lm = order[i];
        if (!lm->no_thread_calls) (void)rt_ldr_call_entry(ctx, lm, DLL_THREAD_DETACH);
        rt_ldr_tls_callbacks(ctx, lm, DLL_THREAD_DETACH);
    }
    rt_ldr_tls_thread_end(ctx);                 /* frees the thread's blocks, forgets the thread */
    rt_loader_unlock();
}
