/* The loader: import descriptors (runtime/core/loader_import.c; specification: loader.h under
 * "imports").  Plan: docs/handoff/rt-contract-loader.md, task 2.  Slot a imports from two
 * native modules (one of them under two spellings) and from slot b; slot c imports a name
 * slot b does not export.
 *
 * Where this file departs from the plan: case 17 patches OriginalFirstThunk to 0xFFFC, not
 * 0xFFF0.  The image is 0x10000 bytes, so an entry at 0xFFF0 is wholly inside it and reads 0,
 * an empty table; 0xFFFC is the first address whose 8 bytes cross the end. */
#include "rt/test_pe.h"
#include "../../core/loader.h"
#include "shim_ids.h"
#include "rttest_ids.h"

#define F(i) RT_TEST_LIFT_RVA(i)
#define UNFILLED UINT64_C(0xBAD0BAD0BAD0BAD0)

static void give77(Ctx *ctx) { ctx->r[0] = 77; GUEST_RET(ctx); }

static void write_slots(void) {
    RtTestPe *b = rt_test_pe_new();
    rt_test_pe_export(b, "Give", 1, F(1));
    rt_test_pe_export(b, "Datum", 2, 0x2200);
    rt_test_pe_export(b, NULL, 9, F(2));
    rt_test_pe_write(b, "t_lift_b.dll");

    RtTestPe *a = rt_test_pe_new();
    rt_test_pe_import(a, "kernel32.dll", "GetLastError", 0, 0x2020);
    rt_test_pe_import(a, "kernel32.dll", "NoSuchFunctionXyz", 0, 0x2028);
    rt_test_pe_import(a, "KERNEL32", NULL, 4242, 0x2040);          /* a second descriptor, another spelling */
    rt_test_pe_import(a, "t_lift_b.dll", "Give", 0, 0x2060);
    rt_test_pe_import(a, "t_lift_b.dll", "Datum", 0, 0x2068);
    rt_test_pe_import(a, "t_lift_b.dll", NULL, 9, 0x2070);
    rt_test_pe_import(a, "rttest.dll", "Poke", 0, 0x2090);
    rt_test_pe_write(a, "t_lift_a.dll");

    RtTestPe *c = rt_test_pe_new();
    rt_test_pe_import(c, "t_lift_b.dll", "Missing", 0, 0x2020);
    rt_test_pe_write(c, "t_lift_c.dll");
}

TEST_SETUP(core, loader_import) { write_slots(); }
TEST(core, loader_import)
{
    RtModule *A = rt_module_find("t_lift_a.dll"), *B = rt_module_find("t_lift_b.dll");
    RtModule *C = rt_module_find("t_lift_c.dll"), *K = rt_module_find("kernel32.dll");
    T_REQUIRE(A != NULL && B != NULL && C != NULL && K != NULL);
    RtLdrModule *LA = rt_ldr_module(A);
    RtModule *out[8];
    const char *d = NULL;
    char where[96];

    T_PLAN(1, "start-up made the trap stubs of the real imports") {
        T_TRUE(rt_disp_sealed());
        T_TRUE(rt_native_import("kernel32.dll", "NoSuchFunctionXyz", 0) != 0);
        T_TRUE(rt_native_import("kernel32.dll", NULL, 4242) != 0);
    }
    T_PLAN(2, "and of nothing else") {
        T_TRAPS(rt_native_import("kernel32.dll", "NeverImportedAbc", 0), "sealed");
    }
    T_PLAN(3, "the imported modules, in descriptor order, each once") {
        rt_ldr_image_map(LA);
        memset(out, 0, sizeof out);
        T_EQ(rt_ldr_import_modules(LA, out, 8), 3);
        T_TRUE(out[0] == K);
        T_TRUE(out[1] == B);
        T_TRUE(out[2] == rt_module_find("rttest.dll"));
        T_TRUE(out[3] == NULL);
    }
    T_PLAN(4, "a short array gets the first ones and the full count") {
        RtModule *sentinel = (RtModule *)(uintptr_t)0x5E5E;
        out[0] = out[1] = sentinel;
        T_EQ(rt_ldr_import_modules(LA, out, 1), 3);
        T_TRUE(out[0] == K);
        T_TRUE(out[1] == sentinel);
        T_EQ(rt_ldr_import_modules(LA, NULL, 0), 3);
    }
    T_PLAN(5, "bind: a shim's stub") {
        rt_ldr_image_map(rt_ldr_module(B));
        rt_ldr_bind(LA);
        T_EQ(LD64(A->base + 0x2020), rt_test_import_slot(RT_ID_kernel32_GetLastError));
        T_TRUE(T_LOGGED(RT_CH_IMPORT, "imports from t_lift_b.dll (lifted) bound"));
    }
    T_PLAN(6, "an import without a shim gets its trap stub") {
        T_EQ(LD64(A->base + 0x2028), rt_native_import("kernel32.dll", "NoSuchFunctionXyz", 0));
        T_TRUE(!strcmp(rt_module_describe(LD64(A->base + 0x2028), where, sizeof where), "kernel32.dll!NoSuchFunctionXyz"));
    }
    T_PLAN(7, "an import by ordinal from a native module") {
        T_EQ(LD64(A->base + 0x2040), rt_native_import("kernel32.dll", NULL, 4242));
    }
    T_PLAN(8, "imports from a lifted module: function, data, by ordinal") {
        T_EQ(LD64(A->base + 0x2060), B->base + F(1));
        T_EQ(LD64(A->base + 0x2068), B->base + 0x2200);
        T_EQ(LD64(A->base + 0x2070), B->base + F(2));
    }
    T_PLAN(9, "the test library's shim") {
        T_EQ(LD64(A->base + 0x2090), rt_test_import_slot(RT_ID_rttest_Poke));
    }
    T_PLAN(10, "the 8 bytes after each descriptor's last slot stay 0") {
        T_EQ(LD64(A->base + 0x2030), 0);
        T_EQ(LD64(A->base + 0x2048), 0);
        T_EQ(LD64(A->base + 0x2078), 0);
        T_EQ(LD64(A->base + 0x2098), 0);
    }
    T_PLAN(11, "no slot keeps the file's fill") {
        unsigned unfilled = 0;
        for (uint32_t rva = 0x2000; rva < 0x2100; rva += 8) unfilled += LD64(A->base + rva) == UNFILLED;
        T_EQ(unfilled, 0);
    }
    T_PLAN(12, "a call through a bound slot reaches the other module") {
        rt_test_lift_set('b', 1, give77);
        RtTestRet r = rt_test_invoke(LD64(A->base + 0x2060), 0, NULL, &d);
        T_EQ(r.violations, 0);
        T_EQ(r.rax, 77);
    }
    T_PLAN(13, "a call through a trap stub names the import") {
        T_TRAPS(rt_test_invoke(LD64(A->base + 0x2028), 0, NULL, &d), "NoSuchFunctionXyz");
    }
    T_PLAN(14, "an import the other lifted module does not export") {
        rt_ldr_image_map(rt_ldr_module(C));
        T_TRAPS(rt_ldr_bind(rt_ldr_module(C)), "Missing");
        T_EQ(LD64(C->base + 0x2020), UNFILLED);                     /* nothing was written here */
    }
}

TEST_SETUP(core, loader_import_unmapped) { write_slots(); }
TEST(core, loader_import_unmapped)
{
    RtLdrModule *LA = rt_ldr_module(rt_module_find("t_lift_a.dll"));
    RtModule *out[8];
    T_PLAN(15, "before the image is built") {
        T_TRAPS(rt_ldr_bind(LA), "not built");
        T_TRAPS(rt_ldr_import_modules(LA, out, 8), "not built");
    }
}

/* Cases 16 and 17: an image that start-up accepts, damaged in guest memory after it is
 * mapped (rt_init itself would stop on the damaged file, and no test body would run). */
TEST_SETUP(core, loader_import_malformed)
{
    RtTestPe *a = rt_test_pe_new();
    rt_test_pe_import(a, "kernel32.dll", "GetLastError", 0, 0x2020);
    rt_test_pe_write(a, "t_lift_a.dll");
}
TEST(core, loader_import_malformed)
{
    RtModule *A = rt_module_find("t_lift_a.dll");
    RtLdrModule *LA = rt_ldr_module(A);
    RtModule *out[8];
    rt_ldr_image_map(LA);
    IMAGE_IMPORT_DESCRIPTOR *desc = rt_ldr_rva(&LA->img, LA->img.dir[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress, sizeof *desc);
    T_REQUIRE(desc != NULL);
    const uint32_t names = desc->OriginalFirstThunk;
    char *dll = (char *)(uintptr_t)rt_ldr_cstr(&LA->img, desc->Name);
    T_REQUIRE(dll != NULL && !strcmp(dll, "kernel32.dll"));
    T_EQ(rt_ldr_import_modules(LA, out, 8), 1);                     /* sound before the damage */

    T_PLAN(16, "a DLL the registry does not have") {
        memcpy(dll, "nosuchlib.dll", 14);
        T_TRAPS(rt_ldr_import_modules(LA, out, 8), "nosuchlib.dll");
        T_TRAPS(rt_ldr_bind(LA), "nosuchlib.dll");
        memcpy(dll, "kernel32.dll", 13);
    }
    T_PLAN(17, "no name table, and a name table that runs out of the image") {
        desc->OriginalFirstThunk = 0;
        T_TRAPS(rt_ldr_bind(LA), "OriginalFirstThunk");
        desc->OriginalFirstThunk = 0xFFFC;
        T_TRAPS(rt_ldr_bind(LA), "outside the image");
        desc->OriginalFirstThunk = names;
        rt_ldr_bind(LA);                                            /* repaired: binds */
        T_EQ(LD64(A->base + 0x2020), rt_test_import_slot(RT_ID_kernel32_GetLastError));
    }
}
