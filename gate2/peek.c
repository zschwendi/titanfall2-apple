/* The Gate 2 driver's view into the loader: see peek.h.
 *
 * STAND-INS.  rt_loader_prescan calls rt_ldr_import_prescan and rt_ldr_tls_prescan for every
 * lifted module whose DLL has an import or a TLS directory.  While those two are stubs in
 * runtime/core (RT_CORE_UNIMPLEMENTED), rt_init cannot get past tier0.dll.  gate2/gate2.mk
 * looks at the two source files and, for each function that is still a stub, compiles the
 * stand-in below (GATE2_STANDIN_IMPORT_PRESCAN, GATE2_STANDIN_TLS_PRESCAN).  A stand-in is
 * an ordinary definition in this object, which comes before librt.a on the link line, so
 * the archive member holding the stub is never pulled in.  Each does what loader.h
 * specifies for its function and nothing more.  When the runtime's function is built the
 * makefile stops defining the macro and the runtime's is linked: nothing here to delete,
 * though the stand-ins can be deleted then.
 */
#include <stdlib.h>
#include <string.h>

#include "../runtime/core/loader.h"
#include "rt/win/teb.h"
#include "peek.h"

/* ---- walking the tables of a flat image ---------------------------------------------------- */

typedef void (*ImportFn)(const char *dll, const char *name, uint32_t ordinal, uint32_t slot_rva, void *user);

/* Every import of `img`, in file order.  Returns how many. */
static unsigned walk_imports(const RtImage *img, ImportFn each, void *user) {
    const IMAGE_DATA_DIRECTORY *d = &img->dir[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const char *mod = img->m->name;
    unsigned n = 0;
    if (!d->Size) return 0;
    for (uint32_t at = d->VirtualAddress; ; at += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const IMAGE_IMPORT_DESCRIPTOR *desc = rt_ldr_rva(img, at, sizeof *desc);
        if (!desc) rt_fatal("%s: import descriptor at rva %#x lies outside the image", mod, at);
        if (!desc->Name && !desc->FirstThunk && !desc->OriginalFirstThunk) break;
        const char *dll = rt_ldr_cstr(img, desc->Name);
        if (!dll) rt_fatal("%s: the DLL name of the import descriptor at rva %#x lies outside the image", mod, at);
        if (!desc->OriginalFirstThunk)
            rt_fatal("%s: the imports from %s have no import name table (a bound-only import table)", mod, dll);
        for (uint32_t i = 0; ; i++) {
            const uint64_t *entry = rt_ldr_rva(img, desc->OriginalFirstThunk + 8 * i, 8);
            if (!entry) rt_fatal("%s: the import name table of %s runs outside the image", mod, dll);
            uint64_t v;
            memcpy(&v, entry, 8);
            if (!v) break;
            const char *name = NULL;
            uint32_t ordinal = 0;
            if (v & IMAGE_ORDINAL_FLAG64) {
                ordinal = (uint32_t)(v & 0xFFFF);
            } else {
                name = v + 2 <= UINT32_MAX ? rt_ldr_cstr(img, (uint32_t)v + 2) : NULL;     /* after the u16 hint */
                if (!name) rt_fatal("%s: import %u from %s has its name outside the image", mod, i, dll);
            }
            if (each) each(dll, name, ordinal, desc->FirstThunk + 8 * i, user);
            n++;
        }
    }
    return n;
}

static bool rva_is_code(const RtImage *img, uint32_t rva) {
    for (unsigned i = 0; i < img->nsections; i++) {
        const RtLdrSection *s = &img->sections[i];
        if (rva >= s->rva && rva - s->rva < s->vsize) return (s->chars & IMAGE_SCN_MEM_EXECUTE) != 0;
    }
    return false;
}

static void scan_exports(const RtImage *img, Gate2Scan *out) {
    const IMAGE_DATA_DIRECTORY *d = &img->dir[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const char *mod = img->m->name;
    if (!d->Size) return;
    const IMAGE_EXPORT_DIRECTORY *e = rt_ldr_rva(img, d->VirtualAddress, sizeof *e);
    if (!e) rt_fatal("%s: the export directory lies outside the image", mod);
    const uint32_t *funcs = rt_ldr_rva(img, e->AddressOfFunctions, 4 * e->NumberOfFunctions);
    const uint32_t *names = rt_ldr_rva(img, e->AddressOfNames, 4 * e->NumberOfNames);
    const uint16_t *ords  = rt_ldr_rva(img, e->AddressOfNameOrdinals, 2 * e->NumberOfNames);
    if (!funcs || !names || !ords) rt_fatal("%s: an export table lies outside the image", mod);

    out->exports = calloc(e->NumberOfFunctions ? e->NumberOfFunctions : 1, sizeof *out->exports);
    if (!out->exports) rt_fatal("out of memory");
    for (uint32_t i = 0; i < e->NumberOfFunctions; i++) {
        uint32_t rva;
        memcpy(&rva, &funcs[i], 4);
        if (!rva) continue;                                     /* an unused ordinal */
        Gate2Export *x = &out->exports[out->nexports++];
        x->ordinal = e->Base + i;
        x->rva = rva;
        x->kind = rva >= d->VirtualAddress && rva - d->VirtualAddress < d->Size ? GATE2_EXP_FORWARDED
                : rva_is_code(img, rva) ? GATE2_EXP_CODE : GATE2_EXP_DATA;
        for (uint32_t k = 0; k < e->NumberOfNames; k++) {
            uint16_t o;
            uint32_t nrva;
            memcpy(&o, &ords[k], 2);
            if (o != i) continue;
            memcpy(&nrva, &names[k], 4);
            x->name = rt_ldr_cstr(img, nrva);
            if (!x->name) rt_fatal("%s: the name of export ordinal %u lies outside the image", mod, x->ordinal);
            break;
        }
    }
}

static void add_import(const char *dll, const char *name, uint32_t ordinal, uint32_t slot_rva, void *user) {
    Gate2Scan *out = user;
    out->imports[out->nimports++] = (Gate2Import){ dll, name, ordinal, slot_rva };
}

void gate2_scan(RtModule *m, Gate2Scan *out) {
    memset(out, 0, sizeof *out);
    out->tls_index = -1;
    RtLdrModule *lm = rt_ldr_module(m);
    if (lm->state == RT_LDR_ABSENT) return;
    RtImage *img = malloc(sizeof *img);                         /* kept: the strings point into it */
    if (!img) rt_fatal("out of memory");
    if (!rt_ldr_image_scan(lm, img)) rt_fatal("%s: the DLL file was there at start-up and is gone", m->name);
    out->present = true;
    out->host_path = lm->host_path;
    out->entry_rva = img->entry_rva;
    out->tls_index = lm->tls_index;
    out->nsections = img->nsections;
    scan_exports(img, out);
    unsigned n = walk_imports(img, NULL, NULL);
    out->imports = calloc(n ? n : 1, sizeof *out->imports);
    if (!out->imports) rt_fatal("out of memory");
    walk_imports(img, add_import, out);
}

void gate2_map_only(RtModule *m) {
    RtLdrModule *lm = rt_ldr_module(m);
    if (lm->state == RT_LDR_ABSENT) rt_fatal("%s has no DLL file: it cannot be mapped", m->name);
    rt_ldr_image_map(lm);
    rt_ldr_image_protect(lm);
}

/* ---- stand-ins for the parts of the pre-scan that are not built (see the top of the file) ---- */

#ifdef GATE2_STANDIN_IMPORT_PRESCAN
static void prescan_one(const char *dll, const char *name, uint32_t ordinal, uint32_t slot_rva, void *user) {
    const RtImage *img = user;
    (void)slot_rva;
    RtModule *from = rt_module_find(dll);
    if (!from) rt_fatal("%s imports from %s, which is not a module of the manifest", img->m->name, dll);
    if (from->native) (void)rt_native_import(dll, name, ordinal);      /* creates the trap stub when there is no shim */
}

void rt_ldr_import_prescan(RtLdrModule *lm, const RtImage *img) {
    (void)lm;
    walk_imports(img, prescan_one, (void *)(uintptr_t)img);
}
#endif

#ifdef GATE2_STANDIN_TLS_PRESCAN
static int32_t next_tls_index;

void rt_ldr_tls_prescan(RtLdrModule *lm, const RtImage *img) {
    (void)img;
    if (next_tls_index >= RT_STATIC_TLS_SLOTS) rt_fatal("%s: no static TLS index is left", lm->m->name);
    lm->tls_index = next_tls_index++;
}
#endif

const char *gate2_standins(void) {
#if defined(GATE2_STANDIN_IMPORT_PRESCAN) && defined(GATE2_STANDIN_TLS_PRESCAN)
    return "rt_ldr_import_prescan, rt_ldr_tls_prescan";
#elif defined(GATE2_STANDIN_IMPORT_PRESCAN)
    return "rt_ldr_import_prescan";
#elif defined(GATE2_STANDIN_TLS_PRESCAN)
    return "rt_ldr_tls_prescan";
#else
    return "";
#endif
}
