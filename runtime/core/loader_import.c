/* The loader: import descriptors (docs/RUNTIME.md section 1.4, section 1.6 step 3).
 * Specification: loader.h, under "imports".
 *
 * No state of its own.  The caller holds the loader lock (or is rt_loader_prescan).  One
 * walk serves the three functions: the descriptors, 20 bytes each, until one whose Name is
 * 0; per descriptor the import NAME table (OriginalFirstThunk), 8 bytes per import until a
 * zero entry.  Entry i is an ordinal (IMAGE_ORDINAL_FLAG64 set: the low 16 bits) or the RVA
 * of a u16 hint followed by the name, and belongs to the slot at FirstThunk + 8 * i.  What
 * the file left in the slots is never read.
 *
 * Fatal, naming the importing module: a descriptor, DLL name, name table, name or slot that
 * is not inside the image; a DLL the registry does not have; a descriptor without a name
 * table (OriginalFirstThunk 0: bound-only import tables are not supported).  The walk is
 * bounded by the image: every step reads further up or stops.
 */
#include <stdio.h>
#include <string.h>

#include "loader.h"

typedef struct {
    const RtLdrModule *lm;
    const RtImage     *img;
    uint32_t next_desc;         /* RVA of the descriptor that next_descriptor reads */
    /* the current descriptor */
    RtModule   *dll;            /* the module it imports from */
    const char *dll_name;       /* as the image spells it */
    uint32_t    names, slots;   /* OriginalFirstThunk, FirstThunk */
    uint32_t    i;              /* the import that next_import reads */
} Walk;

static _Noreturn void bad(const Walk *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static _Noreturn void bad(const Walk *w, const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    rt_fatal("%s: malformed import directory: %s", w->lm->m->name, why);
}

static void walk_begin(Walk *w, const RtLdrModule *lm, const RtImage *img) {
    memset(w, 0, sizeof *w);
    w->lm = lm;
    w->img = img;
    const IMAGE_DATA_DIRECTORY dd = img->dir[IMAGE_DIRECTORY_ENTRY_IMPORT];
    w->next_desc = dd.Size ? dd.VirtualAddress : 0;     /* 0: the image imports nothing */
}

/* Move to the next descriptor; false at the end of the list. */
static bool next_descriptor(Walk *w) {
    if (!w->next_desc) return false;
    IMAGE_IMPORT_DESCRIPTOR d;
    const void *p = rt_ldr_rva(w->img, w->next_desc, sizeof d);
    if (!p) bad(w, "the descriptor at %#x is outside the image (the list has no end)", w->next_desc);
    memcpy(&d, p, sizeof d);
    if (d.Name == 0) { w->next_desc = 0; return false; }
    w->dll_name = rt_ldr_cstr(w->img, d.Name);
    if (!w->dll_name) bad(w, "the DLL name of the descriptor at %#x is outside the image", w->next_desc);
    w->dll = rt_module_find(w->dll_name);
    if (!w->dll)
        rt_fatal("%s imports from %s, which the registry does not have: the manifest (runtime/modules.def, or "
                 "the program's module list) is incomplete", w->lm->m->name, w->dll_name);
    if (d.OriginalFirstThunk == 0)
        bad(w, "the descriptor for %s has no import name table (OriginalFirstThunk is 0): bound-only import "
               "tables are not supported", w->dll_name);
    if (d.FirstThunk == 0) bad(w, "the descriptor for %s has no slots (FirstThunk is 0)", w->dll_name);
    w->names = d.OriginalFirstThunk;
    w->slots = d.FirstThunk;
    w->i = 0;
    w->next_desc += sizeof d;
    return true;
}

/* The next import of the current descriptor: *name, or *name NULL and *ordinal; *slot is the
 * RVA of its slot.  False at the end of the name table. */
static bool next_import(Walk *w, const char **name, uint32_t *ordinal, uint32_t *slot) {
    uint64_t e;
    uint64_t at = (uint64_t)w->names + 8 * (uint64_t)w->i;
    const void *p = at <= UINT32_MAX ? rt_ldr_rva(w->img, (uint32_t)at, 8) : NULL;
    if (!p) bad(w, "the import name table of %s (at %#x, entry %u) runs outside the image", w->dll_name, w->names, w->i);
    memcpy(&e, p, 8);
    if (e == 0) return false;
    *name = NULL;
    *ordinal = 0;
    if (e & IMAGE_ORDINAL_FLAG64) {
        *ordinal = (uint32_t)(e & 0xFFFF);
    } else {
        /* A u16 hint, then the name. */
        if (e >= w->img->size || !(*name = rt_ldr_cstr(w->img, (uint32_t)e + 2)))
            bad(w, "the name of import %u from %s (at %#llx) is outside the image", w->i, w->dll_name, (unsigned long long)e);
    }
    uint64_t s = (uint64_t)w->slots + 8 * (uint64_t)w->i;
    if (s > UINT32_MAX || !rt_ldr_rva(w->img, (uint32_t)s, 8))
        bad(w, "the slot of import %u from %s (at %#llx) is outside the image", w->i, w->dll_name, (unsigned long long)s);
    *slot = (uint32_t)s;
    w->i++;
    return true;
}

static const char *label(const char *name, uint32_t ordinal, char buf[32]) {
    if (name) return name;
    snprintf(buf, 32, "#%u", ordinal);
    return buf;
}

void rt_ldr_import_prescan(RtLdrModule *lm, const RtImage *img) {
    Walk w;
    const char *name;
    uint32_t ordinal, slot;
    walk_begin(&w, lm, img);
    while (next_descriptor(&w))
        while (next_import(&w, &name, &ordinal, &slot))
            /* Before the dispatch table is sealed: this is what creates the trap stub of an
             * import that has no shim.  A lifted module's exports need nothing now. */
            if (w.dll->native) (void)rt_native_import(w.dll->name, name, ordinal);
}

unsigned rt_ldr_import_modules(RtLdrModule *lm, RtModule **out, unsigned max) {
    if (!lm->img.mapped) rt_fatal("rt_ldr_import_modules(%s): the image is not built", lm->m->name);
    Walk w;
    unsigned n = 0;
    walk_begin(&w, lm, &lm->img);
    while (next_descriptor(&w)) {
        /* Two descriptors may name one module ("KERNEL32" and "kernel32.dll"): count it once.
         * The list of earlier ones is the descriptors themselves, walked again. */
        Walk seen;
        bool dup = false;
        walk_begin(&seen, lm, &lm->img);
        while (!dup && next_descriptor(&seen) && seen.next_desc < w.next_desc) dup = seen.dll == w.dll;
        if (dup) continue;
        if (n < max) out[n] = w.dll;
        n++;
    }
    return n;
}

void rt_ldr_bind(RtLdrModule *lm) {
    RtModule *m = lm->m;
    if (!lm->img.mapped) rt_fatal("rt_ldr_bind(%s): the image is not built", m->name);
    Walk w;
    const char *name;
    uint32_t ordinal, slot;
    char lab[32];
    walk_begin(&w, lm, &lm->img);
    while (next_descriptor(&w)) {
        RtModule *dll = w.dll;
        if (!dll->native && !rt_ldr_module(dll)->img.mapped)
            rt_fatal("%s imports from %s, which is %s: its image must be built before the slots of %s are filled",
                     m->name, dll->name, rt_ldr_module(dll)->state == RT_LDR_ABSENT ? "absent (no DLL file)" : "not loaded",
                     m->name);
        unsigned n = 0;
        while (next_import(&w, &name, &ordinal, &slot)) {
            uint64_t v;
            if (dll->native) {
                v = rt_native_import(dll->name, name, ordinal);     /* the shim's stub, or the trap stub */
            } else {
                v = rt_loader_export(dll, name, ordinal);
                if (!v)
                    rt_fatal("%s imports %s!%s, which %s does not export: the build is inconsistent (the two DLLs "
                             "are not from one install)", m->name, dll->name, label(name, ordinal, lab), dll->name);
            }
            ST64(m->base + slot, v);
            n++;
        }
        rt_log(RT_CH_IMPORT, RT_LL_DEBUG, "%s: %u imports from %s (%s) bound, slots at %#llx", m->name, n, dll->name,
               dll->native ? "native" : "lifted", (unsigned long long)(m->base + w.slots));
    }
}
