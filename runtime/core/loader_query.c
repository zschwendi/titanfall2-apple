/* The loader: questions about loaded modules (docs/RUNTIME.md sections 1.3 and 1.5).
 * Specification: rt_module_from_handle, rt_module_handle_w, rt_module_handle_a,
 * rt_module_handle_ex, rt_module_filename_w, rt_module_filename_a, rt_module_from_pc in
 * rt/module.h.
 *
 * No state of its own.  Every function takes the loader lock for the length of the call
 * (it reads RtModule.loaded and changes RtModule.load_count) and runs no guest code.  The
 * last error is set after the lock is released, by the function that fails.
 *
 * DECISIONS where rt/module.h is silent.  A module's file name is ASCII, so the A and W
 * forms differ only in the element type.  The <name> of a system module in
 * C:\Windows\system32\<name> is RtModule.name (lower case); the executable and the lifted
 * and stubbed modules use the manifest's spelling (def->file).  rt_module_filename_* does
 * not ask whether the module is loaded: the shim got `m` from rt_module_from_handle.
 */
#include <stdio.h>
#include <string.h>

#include "loader.h"

#define KNOWN_EX_FLAGS (GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT \
                        | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)

/* `m` if it is loaded, else NULL; m may be NULL. */
static RtModule *if_loaded(RtModule *m) {
    rt_loader_lock();
    if (m && !m->loaded) m = NULL;
    rt_loader_unlock();
    return m;
}

static RtModule *not_found(void) {
    rt_thread_set_last_error(ERROR_MOD_NOT_FOUND);
    return NULL;
}

RtModule *rt_module_from_handle(HMODULE h) {
    if (!h) return rt_module_exe();
    RtModule *m = rt_module_from_address((uint64_t)(uintptr_t)h);
    if (m && m->base != (uint64_t)(uintptr_t)h) m = NULL;          /* exactly the base */
    m = if_loaded(m);
    return m ? m : not_found();
}

RtModule *rt_module_handle_w(const WCHAR *name) {
    if (!name) return rt_module_exe();
    RtModule *m = if_loaded(rt_module_find_w(name));
    return m ? m : not_found();
}

RtModule *rt_module_handle_a(const char *name) {
    if (!name) return rt_module_exe();
    RtModule *m = if_loaded(rt_module_find(name));
    return m ? m : not_found();
}

RtModule *rt_module_from_pc(uint64_t pc) {
    return if_loaded(rt_module_from_address(pc));
}

BOOL rt_module_handle_ex(DWORD flags, const void *name, bool wide, RtModule **out) {
    if (out) *out = NULL;
    if (!out || (flags & ~KNOWN_EX_FLAGS)
        || ((flags & GET_MODULE_HANDLE_EX_FLAG_PIN) && (flags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))) {
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtModule *m;
    if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) m = rt_module_from_address((uint64_t)(uintptr_t)name);   /* NULL: none */
    else if (!name) m = rt_module_exe();
    else m = wide ? rt_module_find_w(name) : rt_module_find(name);

    rt_loader_lock();                   /* one hold for the look and the count */
    if (m && !m->loaded) m = NULL;
    if (m && !(flags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT)) m->load_count++;
    rt_loader_unlock();
    if (!m) { rt_thread_set_last_error(ERROR_MOD_NOT_FOUND); return FALSE; }
    *out = m;
    return TRUE;
}

/* The module's full guest path, ASCII; returns its length. */
static size_t module_path(const RtModule *m, char *out, size_t n) {
    const RtModuleDef *d = m->def;
    int w;
    if (m->kind == RT_MOD_SYSTEM) w = snprintf(out, n, "C:\\Windows\\system32\\%s", m->name);
    else if (m->kind == RT_MOD_EXE || !d->dir || !d->dir[0]) w = snprintf(out, n, "C:\\Titanfall2\\%s", d->file);
    else w = snprintf(out, n, "C:\\Titanfall2\\%s\\%s", d->dir, d->file);
    if (w < 0 || (size_t)w >= n) rt_fatal("the path of module %s is longer than %zu characters", m->name, n);
    return (size_t)w;
}

/* The shared body: `wide` chooses the element type of buf. */
static DWORD filename(RtModule *m, void *buf, DWORD cch, bool wide, const char *who) {
    char path[RT_PATH_MAX];
    if (!m) rt_fatal("%s: null module", who);
    if (!buf && cch > 0) rt_fatal("%s(%s): null buffer of %u characters", who, m->name, cch);
    rt_loader_lock();
    size_t len = module_path(m, path, sizeof path);
    rt_loader_unlock();
    if (cch == 0) { rt_thread_set_last_error(ERROR_INSUFFICIENT_BUFFER); return 0; }
    bool fits = len < cch;                                          /* with the terminator */
    size_t n = fits ? len : cch - 1;
    for (size_t i = 0; i <= n; i++) {
        char c = i < n ? path[i] : 0;
        if (wide) ((WCHAR *)buf)[i] = (WCHAR)(unsigned char)c;
        else      ((char *)buf)[i] = c;
    }
    if (fits) return (DWORD)len;
    rt_thread_set_last_error(ERROR_INSUFFICIENT_BUFFER);
    return cch;
}

DWORD rt_module_filename_w(RtModule *m, WCHAR *buf, DWORD cch) { return filename(m, buf, cch, true, "rt_module_filename_w"); }
DWORD rt_module_filename_a(RtModule *m, char *buf, DWORD cch)  { return filename(m, buf, cch, false, "rt_module_filename_a"); }
