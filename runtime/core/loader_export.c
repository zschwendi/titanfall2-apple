/* The loader: a lifted module's export directory (docs/RUNTIME.md sections 1.4, 1.5).
 * Specification: rt_loader_export in rt/module.h.
 *
 * No state of its own.  Takes the loader lock for the length of the call (rt_loader_lock)
 * and reads only rt_ldr_module(m)->img through rt_ldr_rva and rt_ldr_cstr.  "Loaded" here
 * means that the image is built (img.mapped): rt_ldr_bind looks exports up in a module that
 * is still LOADING, and RtModule.loaded is rt_module_export's check.  Sets no last error and
 * logs nothing (rt_module_export does both).  A directory whose tables do not lie inside the
 * image is fatal ("malformed export directory").
 */
#include <stdio.h>
#include <string.h>

#include "loader.h"

static _Noreturn void malformed(const RtModule *m, const char *what) {
    rt_fatal("%s: malformed export directory: %s", m->name, what);
}

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

GUESTFN rt_loader_export(RtModule *m, const char *name, uint32_t ordinal) {
    rt_loader_lock();
    const RtImage *img = &rt_ldr_module(m)->img;        /* fatal: NULL, a native module */
    if (!img->mapped) rt_fatal("rt_loader_export(%s): the module is not loaded (its image is not built)", m->name);
    if (!name && ordinal > 0xFFFF)
        rt_fatal("rt_loader_export(%s): ordinal %u is above 0xFFFF and there is no name", m->name, ordinal);

    GUESTFN addr = 0;
    const IMAGE_DATA_DIRECTORY dd = img->dir[IMAGE_DIRECTORY_ENTRY_EXPORT];
    IMAGE_EXPORT_DIRECTORY d;
    const uint8_t *p = dd.Size ? rt_ldr_rva(img, dd.VirtualAddress, sizeof d) : NULL;
    if (!dd.Size) goto done;                            /* the module exports nothing */
    if (!p || dd.Size < sizeof d) malformed(m, "the directory is shorter than its header");
    memcpy(&d, p, sizeof d);
    if (d.NumberOfFunctions > img->size / 4 || d.NumberOfNames > img->size / 4) malformed(m, "its counts exceed the image");
    const uint8_t *fns = rt_ldr_rva(img, d.AddressOfFunctions, 4 * d.NumberOfFunctions);
    if (!fns) malformed(m, "the function table is outside the image");

    uint32_t index;                                     /* into the function table */
    if (name) {
        const uint8_t *names = rt_ldr_rva(img, d.AddressOfNames, 4 * d.NumberOfNames);
        const uint8_t *ords = rt_ldr_rva(img, d.AddressOfNameOrdinals, 2 * d.NumberOfNames);
        if (!names || !ords) malformed(m, "the name tables are outside the image");
        uint32_t lo = 0, hi = d.NumberOfNames;          /* the name, if present, is in [lo, hi) */
        index = UINT32_MAX;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            const char *s = rt_ldr_cstr(img, rd32(names + 4 * mid));
            if (!s) malformed(m, "a name is outside the image");
            int c = strcmp(name, s);
            if (c == 0) { index = rd16(ords + 2 * mid); break; }
            if (c < 0) hi = mid; else lo = mid + 1;
        }
        if (index == UINT32_MAX) goto done;             /* no such name */
        if (index >= d.NumberOfFunctions) malformed(m, "a name's ordinal is outside the function table");
    } else {
        if (ordinal < d.Base || ordinal - d.Base >= d.NumberOfFunctions) goto done;
        index = ordinal - d.Base;
    }

    uint32_t rva = rd32(fns + 4 * index);
    if (rva == 0) goto done;                            /* an unused ordinal */
    if (rva >= dd.VirtualAddress && rva - dd.VirtualAddress < dd.Size) {
        /* An RVA inside the export directory is not code or data but the string "DLL.Name". */
        const char *fwd = rt_ldr_cstr(img, rva);
        char label[32];
        snprintf(label, sizeof label, "#%u", ordinal);
        rt_fatal("%s!%s is a forwarded export (to %s): forwarders are not supported, none was expected "
                 "(docs/RUNTIME.md section 1.4)", m->name, name ? name : label, fwd ? fwd : "?");
    }
    if (rva >= img->size) malformed(m, "a function's address is outside the image");
    addr = m->base + rva;
done:
    rt_loader_unlock();
    return addr;
}
