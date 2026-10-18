/* The loader: export lookup on a lifted module (runtime/core/loader_export.c; specification:
 * rt_loader_export in rt/module.h).  Plan: docs/handoff/rt-contract-loader.md, task 1.
 * Slot a has named, unnamed, data and forwarded exports; slot b has none; the corpus DLL is a
 * real linker's export directory. */
#include <pthread.h>

#include "rt/test_pe.h"
#include "../../core/loader.h"

#define F(i) RT_TEST_LIFT_RVA(i)
#define CORPUS_REL "bin/x64_retail/corpus_O1.dll"
#define MAX_NAMES 2048

/* The corpus exports, found by a linear walk that owes nothing to the loader's lookup. */
static const char *names[MAX_NAMES];
static uint64_t    expect[MAX_NAMES];
static unsigned    nnames;

static void walk_corpus(const RtImage *img) {
    const IMAGE_EXPORT_DIRECTORY *d = rt_ldr_rva(img, img->dir[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress, sizeof *d);
    if (!d) return;
    const uint32_t *nm = rt_ldr_rva(img, d->AddressOfNames, 4 * d->NumberOfNames);
    const uint16_t *ords = rt_ldr_rva(img, d->AddressOfNameOrdinals, 2 * d->NumberOfNames);
    const uint32_t *fns = rt_ldr_rva(img, d->AddressOfFunctions, 4 * d->NumberOfFunctions);
    for (uint32_t i = 0; nm && ords && fns && i < d->NumberOfNames && nnames < MAX_NAMES; i++) {
        names[nnames] = rt_ldr_cstr(img, nm[i]);
        expect[nnames++] = img->m->base + fns[ords[i]];
    }
}

static uint64_t find_export(const char *name) {
    for (unsigned i = 0; i < nnames; i++) if (!strcmp(names[i], name)) return expect[i];
    return 0;
}

typedef struct { RtModule *c; unsigned wrong; } Work;

static void *worker(void *arg) {
    Work *w = arg;
    for (unsigned round = 0; round < 200; round++)
        for (unsigned i = 0; i < nnames; i++)
            w->wrong += rt_loader_export(w->c, names[i], 0) != expect[i];
    return NULL;
}

TEST_SETUP(core, loader_export)
{
    RtTestPe *pe = rt_test_pe_new();
    rt_test_pe_export(pe, "Zeta", 5, F(2));
    rt_test_pe_export(pe, "Alpha", 3, 0x2100);
    rt_test_pe_export(pe, "alpha", 9, 0x2108);
    rt_test_pe_export(pe, NULL, 7, F(3));
    rt_test_pe_forward(pe, "Fwd", 4, "OTHER.Name");
    rt_test_pe_write(pe, "t_lift_a.dll");
    rt_test_pe_write(rt_test_pe_new(), "t_lift_b.dll");
    rt_test_install_copy(RT_TEST_CORPUS_DLL, CORPUS_REL);
}

TEST(core, loader_export)
{
    RtModule *A = rt_module_find("t_lift_a.dll"), *B = rt_module_find("t_lift_b.dll");
    RtModule *C = rt_module_find("corpus_O1.dll");
    T_REQUIRE(A != NULL && B != NULL && C != NULL);
    rt_ldr_image_map(rt_ldr_module(A));
    rt_ldr_image_map(rt_ldr_module(B));
    rt_ldr_image_map(rt_ldr_module(C));
    walk_corpus(&rt_ldr_module(C)->img);

    T_PLAN(1, "a data export by name") { T_EQ(rt_loader_export(A, "Alpha", 0), A->base + 0x2100); }
    T_PLAN(2, "a function by name") { T_EQ(rt_loader_export(A, "Zeta", 0), A->base + F(2)); }
    T_PLAN(3, "names are case-sensitive") { T_EQ(rt_loader_export(A, "alpha", 0), A->base + 0x2108); }
    T_PLAN(4, "names the module does not have") {
        T_EQ(rt_loader_export(A, "ALPHA", 0), 0);
        T_EQ(rt_loader_export(A, "Alph", 0), 0);
        T_EQ(rt_loader_export(A, "Alphaa", 0), 0);
        T_EQ(rt_loader_export(A, "", 0), 0);
    }
    T_PLAN(5, "by ordinal") {
        T_EQ(rt_loader_export(A, NULL, 3), A->base + 0x2100);
        T_EQ(rt_loader_export(A, NULL, 5), A->base + F(2));
        T_EQ(rt_loader_export(A, NULL, 7), A->base + F(3));        /* has no name */
    }
    T_PLAN(6, "unused ordinals and ordinals outside the range") {
        T_EQ(rt_loader_export(A, NULL, 6), 0);                     /* RVA 0: not the base */
        T_EQ(rt_loader_export(A, NULL, 8), 0);
        T_EQ(rt_loader_export(A, NULL, 2), 0);                     /* below the Base */
        T_EQ(rt_loader_export(A, NULL, 10), 0);
        T_EQ(rt_loader_export(A, NULL, 0), 0);
    }
    T_PLAN(7, "the ordinal is ignored when a name is given") { T_EQ(rt_loader_export(A, "Zeta", 3), A->base + F(2)); }
    T_PLAN(8, "a forwarded export is fatal") {
        T_TRAPS(rt_loader_export(A, "Fwd", 0), "OTHER.Name");
        T_TRAPS(rt_loader_export(A, NULL, 4), "OTHER.Name");
    }
    T_PLAN(9, "a module without an export directory") {
        T_EQ(rt_loader_export(B, "Alpha", 0), 0);
        T_EQ(rt_loader_export(B, NULL, 1), 0);
    }
    T_PLAN(10, "every name of the corpus") {
        T_TRUE(nnames > 20 && nnames < MAX_NAMES);
        T_TRUE(find_export("fib") != 0);
        T_EQ(rt_loader_export(C, "fib", 0), find_export("fib"));
        unsigned wrong = 0;
        for (unsigned i = 0; i < nnames; i++) wrong += rt_loader_export(C, names[i], 0) != expect[i];
        T_EQ(wrong, 0);
        T_EQ(rt_loader_export(C, "no_such_export", 0), 0);
    }
    T_PLAN(11, "misuse is fatal") {
        T_TRAPS(rt_loader_export(rt_module_find("kernel32.dll"), "x", 0), "native");
        T_TRAPS(rt_loader_export(A, NULL, 0x10000), "ordinal");
        T_TRAPS(rt_loader_export(rt_module_find("t_lift_c.dll"), "x", 0), "t_lift_c.dll");  /* absent: no image */
    }
    T_PLAN(12, "through rt_module_export: last error and one log line") {
        A->loaded = true;
        rt_thread_set_last_error(0);
        T_EQ(rt_module_export(A, "Nope", 0), 0);
        T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);
        T_TRUE(T_LOGGED(RT_CH_IMPORT, "t_lift_a.dll!Nope"));
        T_EQ(rt_module_export(A, "Alpha", 0), A->base + 0x2100);
    }
    T_PLAN(13, "four threads look every name up 200 times") {
        pthread_t th[4];
        Work w[4];
        for (unsigned i = 0; i < 4; i++) {
            w[i] = (Work){ C, 0 };
            T_EQ(pthread_create(&th[i], NULL, worker, &w[i]), 0);
        }
        for (unsigned i = 0; i < 4; i++) {
            pthread_join(th[i], NULL);
            T_EQ(w[i].wrong, 0);
        }
    }
}
