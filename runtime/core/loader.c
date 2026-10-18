/* The loader for lifted modules: start-up, the lock, the per-module state
 * (docs/RUNTIME.md sections 1.3 to 1.6).  Specification: rt/module.h; the split of the
 * loader into files and their internal functions: loader.h.
 *
 * How lifted code becomes part of the program.  The lifter writes, for module M,
 * lifted/M/M_*.c with `uint64_t g_base_M`, `const LiftedEntry M_table[]`,
 * `const size_t M_table_count`, and `const LiftedSkip M_skips[]` with `M_skips_count` for the
 * rows of the table that are stubs of functions it skipped (rt/module.h, RtLiftedCode).
 * The build compiles those files into one object
 * (runtime/rt.mk, rt_lifted_rules) that the host program links, and the generator
 * (runtime/tools/gen_shims.py) writes a module-table row for the `lifted` line of the
 * program's module list whose `code` points at those symbols and whose identity fields
 * come from the DLL the build read.  rt_loader_prescan below is where the two meet: it
 * gives g_base_M the module's base and puts every function of M_table in the dispatch
 * table at base + rva, before the table is sealed.
 */
#include <pthread.h>
#include <stdlib.h>

#include "loader.h"

static RtLdrModule    *lmods;
static unsigned        nlmods;
static bool            scanned;
static pthread_mutex_t lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;

void rt_loader_lock(void)   { if (pthread_mutex_lock(&lock) != 0) rt_fatal("the loader lock cannot be taken"); }
void rt_loader_unlock(void) { if (pthread_mutex_unlock(&lock) != 0) rt_fatal("the loader lock is released by a thread that does not hold it"); }

RtLdrModule *rt_ldr_module(RtModule *m) {
    if (!m) rt_fatal("rt_ldr_module: null module");
    if (m->native) rt_fatal("rt_ldr_module: %s is a native module, the loader has no state for it", m->name);
    if (!scanned || !m->lifted) rt_fatal("rt_ldr_module(%s) before rt_loader_prescan", m->name);
    return m->lifted;
}

static void register_code(RtLdrModule *lm) {
    RtModule *m = lm->m;
    const RtLiftedCode *c = lm->code;
    if (!c || !c->base || !c->table || !c->count || !c->skips || !c->nskips)
        rt_fatal("lifted module %s has no lifted code linked in: its row of the module table has no `code` "
                 "(the generator writes it; see runtime/rt.mk, rt_lifted_rules)", m->name);
    for (size_t i = 0; i < *c->count; i++) {
        if (c->table[i].rva >= m->size)
            rt_fatal("lifted module %s: function table entry %zu (%s_%x) lies outside the image (SizeOfImage %#llx): "
                     "the lifted C was made from another DLL", m->name, i, c->module, c->table[i].rva,
                     (unsigned long long)m->size);
        if (i > 0 && c->table[i].rva <= c->table[i - 1].rva)
            rt_fatal("lifted module %s: function table is not sorted at entry %zu", m->name, i);
    }
    /* Every skipped function has a row (its stub), so the two lists must agree: a reader
     * takes *count - *nskips as the number of lifted functions. */
    for (size_t i = 0, row = 0; i < *c->nskips; i++, row++) {
        while (row < *c->count && c->table[row].rva < c->skips[i].rva) row++;
        if (row == *c->count || c->table[row].rva != c->skips[i].rva)
            rt_fatal("lifted module %s: skipped function %zu (%s_%x) has no row in the function table, or the list "
                     "of skipped functions is not sorted", m->name, i, c->module, c->skips[i].rva);
    }
    *c->base = m->base;
    rt_disp_add_table(m->base, c->table, *c->count);
}

void rt_loader_prescan(void) {
    if (scanned) rt_fatal("rt_loader_prescan called twice");
    for (unsigned i = 0; i < rt_module_count(); i++) nlmods += !rt_module_at(i)->native;
    lmods = calloc(nlmods ? nlmods : 1, sizeof *lmods);
    if (!lmods) rt_fatal("out of memory");

    unsigned n = 0;
    for (unsigned i = 0; i < rt_module_count(); i++) {
        RtModule *m = rt_module_at(i);
        if (m->native) continue;
        RtLdrModule *lm = &lmods[n++];
        lm->m = m;
        lm->code = m->def->code;
        lm->tls_index = -1;
        m->lifted = lm;
        if (m->size % 0x1000) rt_fatal("lifted module %s: size_of_image %#llx is not a multiple of 4 KB", m->name, (unsigned long long)m->size);
        register_code(lm);
        /* The range belongs to the module from now on, loaded or not, so nothing else can be
         * allocated there (a guest VirtualAlloc with an address). */
        if (rt_vm_alloc(m->base, m->size, MEM_RESERVE, PAGE_READWRITE, RT_VM_IMAGE) != m->base)
            rt_fatal("cannot reserve the image of %s at %#llx", m->name, (unsigned long long)m->base);
    }
    scanned = true;

    for (unsigned i = 0; i < nlmods; i++) {
        RtLdrModule *lm = &lmods[i];
        RtImage img;
        if (!rt_ldr_image_scan(lm, &img)) {
            lm->state = RT_LDR_ABSENT;
            rt_log(RT_CH_LOADER, RT_LL_INFO, "%s: no file C:\\Titanfall2\\%s%s%s; the module cannot be loaded in this run",
                   lm->m->name, lm->m->def->dir ? lm->m->def->dir : "", lm->m->def->dir && lm->m->def->dir[0] ? "\\" : "",
                   lm->m->def->file);
            continue;
        }
        lm->state = RT_LDR_UNLOADED;
        if (img.dir[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) rt_ldr_import_prescan(lm, &img);
        if (img.dir[IMAGE_DIRECTORY_ENTRY_TLS].Size) rt_ldr_tls_prescan(lm, &img);
        rt_log(RT_CH_LOADER, RT_LL_INFO, "%s: base %#llx, %zu lifted functions, %zu skipped by the lifter, file %s",
               lm->m->name, (unsigned long long)lm->m->base, *lm->code->count - *lm->code->nskips, *lm->code->nskips,
               lm->host_path);
        rt_ldr_image_release(&img);
    }
    rt_ldr_probe_prescan();
}
