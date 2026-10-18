/* The module registry and native modules (docs/RUNTIME.md sections 1.2 to 1.5).
 * Interface and the layout of a native module: rt/module.h.
 *
 * Everything here happens once, single-threaded, inside rt_init, except the lookups, which
 * only read.  The loader for lifted modules is runtime/core/loader.c.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "core.h"
#include "loader.h"
#include "rt/vm.h"

typedef struct RtNativeStub {
    const char *name;           /* "CreateWindowExW", "#115", or the name given to rt_native_stub */
    uint32_t    ordinal;        /* trap stubs made for an import by ordinal */
    bool        trap;
    LiftedFn    fn;
} RtNativeStub;

static RtModule *mods;
static unsigned  nmods;
static RtModule *exe_module;

/* Trap stub number -> where it lives. */
static struct { RtModule *m; unsigned stub; } traps[RT_TRAP_POOL];
static unsigned ntraps;

unsigned  rt_module_count(void)      { return nmods; }
RtModule *rt_module_at(unsigned i)   { return i < nmods ? &mods[i] : NULL; }
RtModule *rt_module_exe(void)        { return exe_module; }

/* ---- names --------------------------------------------------------------------------------- */

/* Section 1.5: last path component, lower case, ".dll" appended if there is no extension. */
static bool normalise(const char *in, char out[64]) {
    const char *base = in;
    for (const char *p = in; *p; p++) if (*p == '\\' || *p == '/') base = p + 1;
    size_t n = strlen(base);
    if (n == 0 || n > 58) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)base[i];
        if (c >= 0x80) return false;
        out[i] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    out[n] = 0;
    if (!strchr(out, '.')) strcpy(out + n, ".dll");
    return true;
}

/* Section 10.2: the library as a C identifier. */
static void make_lib(const char *name, char out[64]) {
    size_t n = strlen(name);
    if (n > 4 && (!strcmp(name + n - 4, ".dll") || !strcmp(name + n - 4, ".exe"))) n -= 4;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        out[i] = ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') ? c : '_';
    }
    out[n] = 0;
}

RtModule *rt_module_find(const char *name) {
    char norm[64];
    if (!name || !normalise(name, norm)) return NULL;
    for (unsigned i = 0; i < nmods; i++) if (!strcmp(mods[i].name, norm)) return &mods[i];
    return NULL;
}

RtModule *rt_module_find_w(const WCHAR *name) {
    char narrow[512];
    size_t n = 0;
    if (!name) return NULL;
    for (; name[n]; n++) {
        if (name[n] >= 0x80 || n + 1 >= sizeof narrow) return NULL;
        narrow[n] = (char)name[n];
    }
    narrow[n] = 0;
    return rt_module_find(narrow);
}

RtModule *rt_module_from_address(uint64_t addr) {
    for (unsigned i = 0; i < nmods; i++)
        if (addr >= mods[i].base && addr - mods[i].base < mods[i].size) return &mods[i];
    return NULL;
}

char *rt_module_describe(uint64_t addr, char *buf, size_t n) {
    const RtModule *m = rt_module_from_address(addr);
    if (!m) {
        snprintf(buf, n, rt_vm_in_vm_arena(addr) ? "vm:%#llx" : "%#llx", (unsigned long long)addr);
        return buf;
    }
    uint64_t off = addr - m->base;
    if (m->native && off % RT_STUB_SIZE == 0) {
        if (off >= RT_NATIVE_SHIM_STUBS && (off - RT_NATIVE_SHIM_STUBS) / RT_STUB_SIZE < m->nshims) {
            snprintf(buf, n, "%s!%s", m->name, m->shims[(off - RT_NATIVE_SHIM_STUBS) / RT_STUB_SIZE]->name);
            return buf;
        }
        if (off >= RT_NATIVE_TRAP_STUBS && (off - RT_NATIVE_TRAP_STUBS) / RT_STUB_SIZE < m->nstubs) {
            snprintf(buf, n, "%s!%s", m->name, m->stubs[(off - RT_NATIVE_TRAP_STUBS) / RT_STUB_SIZE].name);
            return buf;
        }
    }
    snprintf(buf, n, "%s+%#llx", m->name, (unsigned long long)off);
    return buf;
}

/* ---- native modules -------------------------------------------------------------------------- */

/* A minimal valid PE32+ header with no sections and no export directory, so that guest code
 * that inspects a system DLL's header by hand finds a well-formed one (section 1.3). */
static void write_pe_header(const RtModule *m) {
    uint64_t b = m->base;
    const uint32_t pe = 0x80;
    ST16(b + 0x00, 0x5A4D);                 /* "MZ" */
    ST32(b + 0x3C, pe);                     /* e_lfanew */
    ST32(b + pe + 0x00, 0x00004550);        /* "PE\0\0" */
    ST16(b + pe + 0x04, 0x8664);            /* Machine: x64 */
    ST16(b + pe + 0x06, 0);                 /* NumberOfSections */
    ST16(b + pe + 0x14, 0xF0);              /* SizeOfOptionalHeader */
    ST16(b + pe + 0x16, m->kind == RT_MOD_EXE ? 0x0022 : 0x2022);   /* executable, large address aware, DLL */
    uint64_t o = b + pe + 0x18;             /* optional header */
    ST16(o + 0x00, 0x020B);                 /* PE32+ */
    ST64(o + 0x18, b);                      /* ImageBase */
    ST32(o + 0x20, 0x1000);                 /* SectionAlignment */
    ST32(o + 0x24, 0x200);                  /* FileAlignment */
    ST16(o + 0x28, 6); ST16(o + 0x2A, 1);   /* OS version 6.1 */
    ST16(o + 0x30, 6); ST16(o + 0x32, 1);   /* subsystem version 6.1 */
    ST32(o + 0x38, RT_NATIVE_SLOT);         /* SizeOfImage */
    ST32(o + 0x3C, 0x1000);                 /* SizeOfHeaders */
    ST16(o + 0x44, 2);                      /* Subsystem: Windows GUI */
    ST32(o + 0x6C, 16);                     /* NumberOfRvaAndSizes; all 16 directories stay zero */
}

/* Stub bytes are filler and never executed.  A shim stub reads as `mov eax, i; ret; int3...`
 * so that a disassembly of the address is recognisable; other stubs are all int3. */
static void write_stub_bytes(uint64_t addr, bool shim, uint32_t index) {
    uint8_t *p = (uint8_t *)(uintptr_t)addr;
    memset(p, 0xCC, RT_STUB_SIZE);
    if (shim) { p[0] = 0xB8; memcpy(p + 1, &index, 4); p[5] = 0xC3; }
}

static int cmp_shim(const void *a, const void *b) {
    return strcmp((*(const RtShimEntry *const *)a)->name, (*(const RtShimEntry *const *)b)->name);
}

static void collect_shims(RtModule *m, const RtShimEntry *table, unsigned count, bool counting) {
    for (unsigned i = 0; i < count; i++) {
        if (strcmp(table[i].lib, m->lib)) continue;
        if (!counting) m->shims[m->nshims] = &table[i];
        m->nshims++;
    }
}

static void build_native(RtModule *m, const RtConfig *cfg) {
    if (rt_vm_alloc(m->base, RT_NATIVE_SLOT, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_IMAGE) != m->base)
        rt_fatal("cannot map native module %s at %#llx", m->name, (unsigned long long)m->base);
    write_pe_header(m);

    m->nshims = 0;
    collect_shims(m, rt_shim_table, rt_shim_count, true);
    collect_shims(m, cfg->extra_shims, cfg->n_extra_shims, true);
    if (m->nshims > (RT_NATIVE_TRAP_STUBS - RT_NATIVE_SHIM_STUBS) / RT_STUB_SIZE)
        rt_fatal("%s has %u shims, more than its stub area holds", m->name, m->nshims);
    m->shims = calloc(m->nshims ? m->nshims : 1, sizeof *m->shims);
    if (!m->shims) rt_fatal("out of memory");
    m->nshims = 0;
    collect_shims(m, rt_shim_table, rt_shim_count, false);
    collect_shims(m, cfg->extra_shims, cfg->n_extra_shims, false);
    qsort(m->shims, m->nshims, sizeof *m->shims, cmp_shim);

    for (unsigned i = 0; i < m->nshims; i++) {
        if (i > 0 && !strcmp(m->shims[i]->name, m->shims[i - 1]->name))
            rt_fatal("%s!%s is implemented twice (%s and %s)", m->name, m->shims[i]->name,
                     m->shims[i]->file, m->shims[i - 1]->file);
        uint64_t stub = m->base + RT_NATIVE_SHIM_STUBS + (uint64_t)RT_STUB_SIZE * i;
        write_stub_bytes(stub, true, i);
        rt_disp_add(stub, m->shims[i]->thunk);
    }
    m->loaded = true;
}

void rt_module_registry_init(const RtConfig *cfg) {
    if (mods) rt_fatal("rt_module_registry_init called twice");
    nmods = rt_module_def_count + cfg->n_extra_modules;
    mods = calloc(nmods, sizeof *mods);
    if (!mods) rt_fatal("out of memory");

    for (unsigned i = 0; i < nmods; i++) {
        RtModule *m = &mods[i];
        m->def = i < rt_module_def_count ? &rt_module_defs[i] : &cfg->extra_modules[i - rt_module_def_count];
        m->kind = m->def->kind;
        m->native = m->kind != RT_MOD_LIFTED;
        if (!normalise(m->def->file, m->name)) rt_fatal("module name '%s' is too long or not ASCII", m->def->file);
        make_lib(m->name, m->lib);
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(mods[j].lib, m->lib)) rt_fatal("modules %s and %s are the same library", mods[j].name, m->name);
        if (m->kind == RT_MOD_EXE) {
            if (exe_module) rt_fatal("the manifest has two executables (%s and %s)", exe_module->name, m->name);
            exe_module = m;
        }
    }
    if (!exe_module) rt_fatal("the manifest (runtime/modules.def) has no `exe` module");

    /* Bases (section 1.2): lifted modules in manifest order, then native modules, each 64 KB
     * aligned with a 64 KB gap, whether or not the module is ever loaded.  The lifted rows
     * written `place=late` come last, from RT_IMAGE_LATE_BASE on (rt/module.h): they move no
     * base of the first two groups, and no later row of those groups moves theirs. */
    uint64_t cursor = RT_IMAGE_ARENA_BASE;
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 2) {
            if (cursor > RT_IMAGE_LATE_BASE)
                rt_fatal("the lifted and the native modules end at %#llx, inside the late group of the image arena "
                         "(RT_IMAGE_LATE_BASE, %#llx)", (unsigned long long)cursor, (unsigned long long)RT_IMAGE_LATE_BASE);
            cursor = RT_IMAGE_LATE_BASE;
        }
        for (unsigned i = 0; i < nmods; i++) {
            RtModule *m = &mods[i];
            if (m->native && (m->def->flags & RT_MODDEF_LATE)) rt_fatal("native module %s is marked late", m->name);
            int group = m->native ? 1 : (m->def->flags & RT_MODDEF_LATE) ? 2 : 0;
            if (group != pass) continue;
            m->size = m->native ? RT_NATIVE_SLOT : m->def->size_of_image;
            if (m->size == 0) rt_fatal("lifted module %s has no size_of_image in the manifest", m->name);
            m->base = cursor;
            cursor = ((cursor + m->size + 0xFFFF) & ~UINT64_C(0xFFFF)) + 0x10000;
            if (cursor > RT_IMAGE_ARENA_BASE + RT_IMAGE_ARENA_SIZE) rt_fatal("the modules do not fit in the image arena");
        }
    }

    for (unsigned i = 0; i < nmods; i++) if (mods[i].native) build_native(&mods[i], cfg);
    ST64(rt_process_block() + RT_PEB_IMAGE_BASE, exe_module->base);

    /* Every shim must have found its module. */
    for (int t = 0; t < 2; t++) {
        const RtShimEntry *table = t ? cfg->extra_shims : rt_shim_table;
        unsigned count = t ? cfg->n_extra_shims : rt_shim_count;
        for (unsigned i = 0; i < count; i++) {
            bool found = false;
            for (unsigned j = 0; j < nmods && !found; j++) found = mods[j].native && !strcmp(mods[j].lib, table[i].lib);
            if (!found) rt_fatal("shim %s!%s (%s) belongs to no native module of the manifest",
                                 table[i].lib, table[i].name, table[i].file);
        }
    }
}

uint64_t rt_native_stub(RtModule *m, const char *name, LiftedFn fn) {
    if (!m || !m->native) rt_fatal("rt_native_stub(%s): not a native module", name);
    if (rt_disp_sealed())
        rt_fatal("rt_native_stub(%s!%s) after the dispatch table was sealed; create stubs during rt_init "
                 "(RtConfig.before_seal)", m->name, name);
    if (m->nstubs >= (RT_NATIVE_VTABLES - RT_NATIVE_TRAP_STUBS) / RT_STUB_SIZE)
        rt_fatal("%s has no room for another native stub", m->name);
    RtNativeStub *grown = realloc(m->stubs, (m->nstubs + 1) * sizeof *grown);
    if (!grown) rt_fatal("out of memory");
    m->stubs = grown;
    m->stubs[m->nstubs] = (RtNativeStub){ .name = name, .fn = fn };
    uint64_t addr = m->base + RT_NATIVE_TRAP_STUBS + (uint64_t)RT_STUB_SIZE * m->nstubs;
    m->nstubs++;
    write_stub_bytes(addr, false, 0);
    rt_disp_add(addr, fn);
    return addr;
}

static int find_shim(const RtModule *m, const char *name, uint32_t ordinal) {
    for (unsigned i = 0; i < m->nshims; i++) {
        if (name ? !strcmp(m->shims[i]->name, name) : (ordinal && m->shims[i]->ordinal == ordinal)) return (int)i;
    }
    return -1;
}

uint64_t rt_native_import(const char *dll, const char *name, uint32_t ordinal) {
    RtModule *m = rt_module_find(dll);
    char label[32];
    if (!name) snprintf(label, sizeof label, "#%u", ordinal);
    if (!m || !m->native)
        rt_fatal("import %s!%s: %s is not a native module of the manifest (runtime/modules.def)",
                 dll, name ? name : label, dll);
    if (!name && ordinal == 0) rt_fatal("import from %s with neither a name nor an ordinal", m->name);

    int i = find_shim(m, name, ordinal);
    if (i >= 0) return m->base + RT_NATIVE_SHIM_STUBS + (uint64_t)RT_STUB_SIZE * (unsigned)i;

    for (unsigned j = 0; j < m->nstubs; j++) {
        const RtNativeStub *s = &m->stubs[j];
        if (s->trap && (name ? (s->ordinal == 0 && !strcmp(s->name, name)) : s->ordinal == ordinal))
            return m->base + RT_NATIVE_TRAP_STUBS + (uint64_t)RT_STUB_SIZE * j;
    }

    if (rt_disp_sealed())
        rt_fatal("import %s!%s has no shim and no trap stub, and the dispatch table is sealed: trap stubs "
                 "are created during rt_init (the loader's pre-scan, or RtConfig.before_seal)",
                 m->name, name ? name : label);
    if (ntraps >= RT_TRAP_POOL) rt_fatal("more than %d distinct unimplemented imports; raise RT_TRAP_POOL", RT_TRAP_POOL);
    char *owned = strdup(name ? name : label);
    if (!owned) rt_fatal("out of memory");
    unsigned index = ntraps++;
    uint64_t addr = rt_native_stub(m, owned, rt_trap_pool_fn(index));
    m->stubs[m->nstubs - 1].trap = true;
    m->stubs[m->nstubs - 1].ordinal = name ? 0 : ordinal;
    traps[index].m = m;
    traps[index].stub = m->nstubs - 1;
    return addr;
}

bool rt_native_trap_info(unsigned i, const RtModule **module, const char **name) {
    if (i >= ntraps) return false;
    *module = traps[i].m;
    *name = traps[i].m->stubs[traps[i].stub].name;
    return true;
}

bool rt_module_stubs_all_halt(const RtModule *m) {
    if (!m || !m->native) return false;
    for (unsigned j = 0; j < m->nstubs; j++) if (!m->stubs[j].trap) return false;
    return true;
}

bool rt_module_lookup_stops(const RtModule *m) {
    return m && m->kind == RT_MOD_STUBBED;      /* every stubbed module, with no exception by name */
}

GUESTFN rt_module_export(RtModule *m, const char *name, uint32_t ordinal) {
    if (!m) rt_fatal("rt_module_export: null module");
    GUESTFN addr = 0;
    if (m->native) {
        /* The names the C runtime probes are answered from the policy table (loader_probe.c). */
        int policy = name ? rt_ldr_probe(m, name, &addr) : 0;
        int i = policy == 0 ? find_shim(m, name, ordinal) : -1;
        if (i >= 0) addr = m->base + RT_NATIVE_SHIM_STUBS + (uint64_t)RT_STUB_SIZE * (unsigned)i;
        /* A STUBBED MODULE (a game DLL that was not lifted and not read: originsdk.dll, the two
         * GFSDK libraries, d3dcompiler_46.dll) NEVER ANSWERS "NOT EXPORTED".  A look-up that
         * finds no shim (for originsdk.dll the only shims are the posted stand-ins a program
         * handed to rt_init) stops the run here, by name or by ordinal, whatever the case of the
         * name; a trap stub of the same name is not an answer either.  Finding 1 of
         * docs/reviews/night-2026-10-11-code-review.md: NULL was a way around the halting stubs. */
        else if (rt_module_lookup_stops(m)) rt_export_unimplemented(m, name, ordinal);
    } else {
        if (!m->loaded) rt_fatal("rt_module_export on %s, which is not loaded", m->name);
        addr = rt_loader_export(m, name, ordinal);
    }
    /* Every lookup, at debug level: which names a module asks for at run time is what a load
     * log is read for, and the call trace prints the name's address only (the argument is a
     * name or an ordinal).  The warning below is printed once per name. */
    if (RT_LOG_ON(RT_CH_IMPORT, RT_LL_DEBUG)) {
        char what[160], answer[32];
        if (name) snprintf(what, sizeof what, "%s", name);
        else      snprintf(what, sizeof what, "#%u", ordinal);
        if (addr) snprintf(answer, sizeof answer, "%#llx", (unsigned long long)addr);
        rt_log(RT_CH_IMPORT, RT_LL_DEBUG, "GetProcAddress(%s, %s) = %s", m->name, what, addr ? answer : "NULL");
    }
    if (!addr) {
        char key[160];
        if (name) snprintf(key, sizeof key, "%s!%s", m->name, name);
        else      snprintf(key, sizeof key, "%s!#%u", m->name, ordinal);
        rt_log_once(RT_CH_IMPORT, RT_LL_WARN, key, "GetProcAddress: %s is not provided; the caller gets NULL", key);
        rt_thread_set_last_error(ERROR_PROC_NOT_FOUND);
    }
    return addr;
}
