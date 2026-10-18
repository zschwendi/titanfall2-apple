/* The loader: start-up, the DLL file and its image (runtime/core/loader.c, loader_image.c;
 * internal interface runtime/core/loader.h).  The subject is the test corpus DLL, a real
 * linker's output with real lifted code; the last test calls its code. */
#include <stdio.h>
#include <stdlib.h>

#include "rt/test_pe.h"
#include "rt/vm.h"
#include "../../core/loader.h"

#define CORPUS_REL "bin/x64_retail/corpus_O1.dll"

static uint8_t file[4 << 20];
static size_t  file_size;

static void read_corpus(void) {
    FILE *f = fopen(RT_TEST_CORPUS_DLL, "rb");
    if (!f) { fprintf(stderr, "no %s: run tests/corpus/build.sh\n", RT_TEST_CORPUS_DLL); abort(); }
    file_size = fread(file, 1, sizeof file, f);
    fclose(f);
}

static uint32_t f32(size_t off) { uint32_t v; memcpy(&v, file + off, 4); return v; }

/* The export `name` of a mapped image, found by walking the directory here (linear search:
 * independent of the loader's own lookup). */
static uint64_t find_export(const RtImage *img, const char *name) {
    const IMAGE_EXPORT_DIRECTORY *d = rt_ldr_rva(img, img->dir[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress, sizeof *d);
    if (!d) return 0;
    const uint32_t *names = rt_ldr_rva(img, d->AddressOfNames, 4 * d->NumberOfNames);
    const uint16_t *ords = rt_ldr_rva(img, d->AddressOfNameOrdinals, 2 * d->NumberOfNames);
    const uint32_t *fns = rt_ldr_rva(img, d->AddressOfFunctions, 4 * d->NumberOfFunctions);
    for (uint32_t i = 0; names && ords && fns && i < d->NumberOfNames; i++)
        if (!strcmp(rt_ldr_cstr(img, names[i]), name)) return img->m->base + fns[ords[i]];
    return 0;
}

TEST_SETUP(core, loader_prescan) { rt_test_install_copy(RT_TEST_CORPUS_DLL, CORPUS_REL); }
TEST(core, loader_prescan)
{
    RtModule *m = rt_module_find("corpus_O1.dll");
    T_REQUIRE(m != NULL && !m->native && m->kind == RT_MOD_LIFTED);
    RtLdrModule *lm = rt_ldr_module(m);
    T_TRUE(lm->m == m && lm->code == m->def->code);
    T_EQ(lm->state, RT_LDR_UNLOADED);
    T_TRUE(!m->loaded && m->load_count == 0 && !lm->img.mapped);
    T_EQ(lm->tls_index, -1);
    T_TRUE(strlen(lm->host_path) > strlen(CORPUS_REL) && !strcmp(lm->host_path + strlen(lm->host_path) - strlen(CORPUS_REL), CORPUS_REL));

    /* The lifted code is linked and registered: the base variable holds the base and every
     * function of the table is a dispatch key at base + rva. */
    T_EQ(*lm->code->base, m->base);
    T_TRUE(*lm->code->count > 100);
    for (size_t i = 0; i < *lm->code->count; i++)
        if (rt_disp_find(m->base + lm->code->table[i].rva) != lm->code->table[i].fn) { T_TRUE(!"table entry not registered"); break; }
    T_TRUE(rt_disp_find(m->base) == NULL);

    /* The range is reserved for the module, nothing is committed, nobody else can have it. */
    MEMORY_BASIC_INFORMATION mbi;
    T_EQ(rt_vm_query(m->base + 0x1000, &mbi), sizeof mbi);
    T_EQ(mbi.State, MEM_RESERVE);
    T_EQ(mbi.Type, MEM_IMAGE);
    T_EQ((uint64_t)(uintptr_t)mbi.AllocationBase, m->base);
    T_EQ(rt_vm_alloc(m->base, 0x1000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE), 0);

    /* The slots without a file are absent; so is nothing else. */
    T_EQ(rt_ldr_module(rt_module_find("t_lift_a.dll"))->state, RT_LDR_ABSENT);
    T_EQ(rt_ldr_module(rt_module_find("t_lift_c.dll"))->host_path[0], 0);
    T_TRAPS(rt_ldr_module(rt_module_find("kernel32.dll")), "native module");
    T_TRAPS(rt_ldr_module(NULL), "null module");
    T_TRAPS(rt_loader_prescan(), "called twice");

    /* The loader lock is recursive. */
    rt_loader_lock(); rt_loader_lock(); rt_loader_unlock(); rt_loader_unlock();
    T_TRAPS(rt_loader_unlock(), "does not hold it");
}

TEST_SETUP(core, loader_image_scan) { rt_test_install_copy(RT_TEST_CORPUS_DLL, CORPUS_REL); }
TEST(core, loader_image_scan)
{
    read_corpus();
    RtLdrModule *lm = rt_ldr_module(rt_module_find("corpus_O1.dll"));
    RtImage img;
    T_REQUIRE(rt_ldr_image_scan(lm, &img));
    uint32_t nt = f32(0x3C);
    T_TRUE(!img.mapped && img.m == lm->m && img.bytes != (uint8_t *)(uintptr_t)lm->m->base);
    T_EQ(img.size, lm->m->size);
    T_EQ(img.size, f32(nt + 24 + 56));
    T_EQ(img.timestamp, f32(nt + 8));
    T_EQ(img.entry_rva, f32(nt + 24 + 16));
    T_EQ(img.preferred_base, UINT64_C(0x180000000));
    T_TRUE(img.nsections >= 3 && !strcmp(img.sections[0].name, ".text") && img.sections[0].rva == 0x1000);
    T_TRUE(img.dir[IMAGE_DIRECTORY_ENTRY_EXPORT].Size != 0 && img.dir[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size != 0);
    T_EQ(img.dir[IMAGE_DIRECTORY_ENTRY_IMPORT].Size, 0);
    /* Flat layout: headers at 0, each section's raw bytes at its RVA, zero beyond them. */
    T_TRUE(!memcmp(img.bytes, file, img.headers_size));
    for (unsigned i = 0; i < img.nsections; i++) {
        const RtLdrSection *s = &img.sections[i];
        uint32_t n = s->raw_size < s->vsize ? s->raw_size : s->vsize;
        T_TRUE(!memcmp(img.bytes + s->rva, file + s->raw_off, n));
        if (s->vsize > n) T_EQ(img.bytes[s->rva + s->vsize - 1], 0);
    }
    /* Bounds of the accessors. */
    T_TRUE(rt_ldr_rva(&img, 0, img.size) == img.bytes);
    T_TRUE(rt_ldr_rva(&img, img.size, 0) == img.bytes + img.size);
    T_TRUE(rt_ldr_rva(&img, img.size, 1) == NULL);
    T_TRUE(rt_ldr_rva(&img, 0x1000, img.size) == NULL);
    T_TRUE(rt_ldr_rva(&img, 0xFFFFFFF0u, 0x20) == NULL);
    T_TRUE(rt_ldr_cstr(&img, img.size) == NULL);
    T_TRUE(rt_ldr_cstr(&img, img.size - 1) != NULL);            /* the last byte is 0: an empty string */
    T_TRUE(!strcmp(rt_ldr_cstr(&img, nt + 24 + (f32(nt + 20) & 0xFFFF)), ".text"));     /* first section header */
    rt_ldr_image_release(&img);
    T_TRUE(img.bytes == NULL);

    /* A file that is not the one the lifted code was made from, or not a DLL this loader
     * handles, is refused, each with its reason.  The file is re-read at every scan. */
#define REFUSED(patch, text) do { read_corpus(); patch; rt_test_install_write(CORPUS_REL, file, file_size); \
                                  T_TRAPS(rt_ldr_image_scan(lm, &img), text); } while (0)
    REFUSED(file[file_size - 1] ^= 1, "SHA-256");
    REFUSED(file_size += 16, "bytes and the lifted code was made");
    REFUSED(file[nt + 8] ^= 1, "PE timestamp");
    REFUSED(file[nt + 24 + 56 + 1] += 0x10, "SizeOfImage");
    REFUSED(file[0] = 'X', "does not start with MZ");
    REFUSED(file[nt] = 'Q', "no PE signature");
    REFUSED(file[nt + 4] = 0x4C, "not x64");
    REFUSED(file[nt + 24] = 0x0B; file[nt + 25] = 0x01, "not PE32+");
    REFUSED(file[nt + 4 + 18 + 1] &= (uint8_t)~0x20, "not a DLL");
    REFUSED(file[0x3C + 2] = 0x7F, "outside the file");
#undef REFUSED
    /* A file that is gone: not there for a scan, fatal for a load. */
    char dir[RT_PATH_MAX];
    snprintf(dir, sizeof dir, "%s/bin/x64_retail", rt_test_install_root());
    T_EQ(remove(lm->host_path), 0);
    rt_path_invalidate(dir);
    T_TRUE(!rt_ldr_image_scan(lm, &img));
    T_TRAPS(rt_ldr_image_map(lm), "is gone now");
}

TEST_SETUP(core, loader_image_map) { rt_test_install_copy(RT_TEST_CORPUS_DLL, CORPUS_REL); }
TEST(core, loader_image_map)
{
    read_corpus();
    RtModule *m = rt_module_find("corpus_O1.dll");
    RtLdrModule *lm = rt_ldr_module(m);
    rt_ldr_image_map(lm);
    const RtImage *img = &lm->img;
    T_TRUE(img->mapped && img->bytes == (uint8_t *)(uintptr_t)m->base);
    T_TRUE(!m->loaded && lm->state == RT_LDR_UNLOADED);          /* the caller's to change */
    T_TRAPS(rt_ldr_image_map(lm), "already built");
    T_TRAPS(rt_ldr_image_release(&lm->img), "mapped image");

    /* Headers as in the file, except ImageBase, which is where the image is. */
    uint32_t nt = f32(0x3C);
    T_EQ(LD16(m->base), 0x5A4D);
    T_EQ(LD64(m->base + nt + 24 + 24), m->base);
    T_TRUE(!memcmp(img->bytes, file, nt + 24 + 24));

    /* Every DIR64 slot holds the file's value moved by base - ImageBase; count them here. */
    IMAGE_DATA_DIRECTORY rd = img->dir[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    uint64_t delta = m->base - img->preferred_base;
    unsigned nrel = 0, bad = 0;
    for (uint32_t off = 0; off < rd.Size; ) {
        uint32_t page = LD32(m->base + rd.VirtualAddress + off), bsize = LD32(m->base + rd.VirtualAddress + off + 4);
        for (uint32_t e = 8; e < bsize; e += 2) {
            uint16_t v = LD16(m->base + rd.VirtualAddress + off + e);
            if (v >> 12 != IMAGE_REL_BASED_DIR64) continue;
            uint32_t rva = page + (v & 0xFFF);
            uint64_t before = 0;
            for (unsigned i = 0; i < img->nsections; i++) {
                const RtLdrSection *s = &img->sections[i];
                if (rva >= s->rva && rva + 8 <= s->rva + s->raw_size) memcpy(&before, file + s->raw_off + (rva - s->rva), 8);
            }
            nrel++;
            bad += LD64(m->base + rva) != before + delta || before - img->preferred_base >= img->size;
        }
        off += bsize;
    }
    T_TRUE(nrel > 10);
    T_EQ(bad, 0);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "relocations"));

    /* Step 5: the VM map says what the section headers say. */
    MEMORY_BASIC_INFORMATION mbi;
    T_EQ(rt_vm_query(m->base + 0x1000, &mbi), sizeof mbi);
    T_EQ(mbi.State, MEM_COMMIT);
    T_EQ(mbi.Protect, PAGE_READWRITE);                  /* until rt_ldr_image_protect */
    rt_ldr_image_protect(lm);
    T_EQ(rt_vm_query(m->base, &mbi), sizeof mbi);
    T_EQ(mbi.Protect, PAGE_READONLY);
    T_EQ(mbi.Type, MEM_IMAGE);
    for (unsigned i = 0; i < img->nsections; i++) {
        const RtLdrSection *s = &img->sections[i];
        DWORD want = (s->chars & IMAGE_SCN_MEM_WRITE) ? PAGE_READWRITE
                   : (s->chars & IMAGE_SCN_MEM_EXECUTE) ? PAGE_EXECUTE_READ : PAGE_READONLY;
        T_EQ(rt_vm_query(m->base + s->rva, &mbi), sizeof mbi);
        T_EQ(mbi.Protect, want);
        T_EQ(mbi.State, MEM_COMMIT);
    }
}

/* The proof that lifted code is linked, registered and runs on its own image: exports of
 * the corpus called through the guest ABI.  tests/corpus/calls.c and switches.c say what
 * they compute. */
static uint32_t ref_fib(uint32_t n) { return n < 2 ? n : ref_fib(n - 1) + ref_fib(n - 2); }

TEST_SETUP(core, loader_corpus_call) { rt_test_install_copy(RT_TEST_CORPUS_DLL, CORPUS_REL); }
TEST(core, loader_corpus_call)
{
    RtModule *m = rt_module_find("corpus_O1.dll");
    RtLdrModule *lm = rt_ldr_module(m);
    rt_ldr_image_map(lm);
    rt_ldr_image_protect(lm);
    /* fib(n): recursion through CALL_DIRECT on the guest stack. */
    uint64_t fib = find_export(&lm->img, "fib");
    T_REQUIRE(fib != 0 && rt_module_from_address(fib) == m);
    T_TRUE(rt_disp_find(fib) != NULL);
    const char *detail = NULL;
    RtTestArg a = { RT_AC_U32, 4, { .u = 13 } };
    RtTestRet r = rt_test_invoke(fib, 1, &a, &detail);
    T_EQ(r.violations, 0);
    T_EQ((uint32_t)r.rax, ref_fib(13));
    T_EQ((uint32_t)r.rax, 233);

    /* fold(v, n, which): acc = 1; acc = kOps[which](acc, v[i]).  kOps is a table of code
     * addresses in .rdata, so this works only if the table was relocated to this base and
     * each target is in the dispatch table. */
    uint64_t fold = find_export(&lm->img, "fold");
    T_REQUIRE(fold != 0);
    static const int32_t v[3] = { 3, 5, 7 };
    static const uint32_t expect[4] = { 16, (uint32_t)-14, 105, 1 ^ 3 ^ 5 ^ 7 };
    for (unsigned which = 0; which < 4; which++) {
        RtTestArg fa[3] = { { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)v } }, { RT_AC_U32, 4, { .u = 3 } }, { RT_AC_U32, 4, { .u = which } } };
        r = rt_test_invoke(fold, 3, fa, &detail);
        T_EQ(r.violations, 0);
        T_EQ((uint32_t)r.rax, expect[which]);
    }
    char where[96];
    T_TRUE(strstr(rt_module_describe(fib, where, sizeof where), "corpus_o1.dll+0x") == where);
    T_TRUE(find_export(&lm->img, "no_such_export") == 0);
}
