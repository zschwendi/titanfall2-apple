/* The rule about EA's SDK as the runtime keeps it BY DEFAULT: a program that hands no
 * stand-in to rt_init (this test's runtime: every program but `gate3 run`, and `gate3 run`
 * with GATE3_NO_STANDIN_PROGRESSIVE_INSTALL=1) answers NO import of originsdk.dll.  All 29
 * names the game's DLLs import (origin_names.h), the one of stand-in 2 among them, are the
 * stub that halts the run and names the import.
 *
 * Expected values: the report's text is docs/RUNTIME.md section 9.1 ("FATAL unimplemented
 * import <dll>!<name>", as runtime/tests/core/dispatch.c expects it); the names are
 * origin_names.h; that nothing answers is the rules file ("Three things nobody does") and
 * the brief of unit gate3-run-5, section 1. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"

TEST_SETUP(standins, origin_all_halt) { origin_write_importer(); }

TEST(standins, origin_all_halt)
{
    Ctx *ctx = T_CTX();
    RtModule *O = rt_module_find("originsdk.dll");
    T_REQUIRE(O != NULL);
    unsigned i;

    T_CASE("originsdk.dll is a module of the runtime with no shim at all");
    T_EQ(O->kind, RT_MOD_STUBBED);
    T_EQ(O->nshims, 0);
    for (i = 0; i < rt_shim_count; i++) T_TRUE(strcmp(rt_shim_table[i].lib, "originsdk") != 0);

    T_CASE("a lifted module that imports all 29 loads, and every slot is a stub of the halting kind");
    RtModule *A = rt_module_load_a(ctx, "t_lift_a.dll", 0);
    T_REQUIRE(A != NULL);
    for (i = 0; i < ORIGIN_NAMES; i++) {
        uint64_t slot = LD64(A->base + ORIGIN_SLOT(i));
        T_EQ(slot, rt_native_import("originsdk.dll", origin_names[i], 0));
        T_TRUE(slot >= O->base + RT_NATIVE_TRAP_STUBS);
    }

    T_CASE("without the stand-in's row its import halts as before, with the arguments of the observed call in the report");
    uint8_t byte = 0;
    const RtTestArg a[3] = {
        { RT_AC_U64, 8, { .u = 0 } }, { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)&byte } }, { RT_AC_U64, 8, { .u = 0xffffffffu } },
    };
    char want[160];
    T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(0)), 3, a, NULL), "FATAL unimplemented import originsdk.dll!OriginIsProgressiveInstallationAvailableSync\n");
    snprintf(want, sizeof want, "rcx=0000000000000000 rdx=%016llx r8=00000000ffffffff", (unsigned long long)(uintptr_t)&byte);
    T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(0)), 3, a, NULL), want);
    T_EQ(byte, 0);

    T_CASE("every import of originsdk.dll halts, by name");
    for (i = 1; i < ORIGIN_NAMES; i++)
        T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(i)), 0, NULL, NULL), origin_halt_text(i));

    /* Finding 1 of docs/reviews/night-2026-10-11-code-review.md: the look-up by name was a way
     * around the stubs above (it answered NULL).  The rule and where its text comes from are in
     * origin_getproc.c; here, the runtime with no stand-in: all 29 names stop, the three that
     * have a stand-in elsewhere among them, and so does an ordinal. */
    T_CASE("with no stand-in, GetProcAddress on originsdk.dll stops the run for every one of the 29 names and never answers NULL");
    const HMODULE h = (HMODULE)(uintptr_t)O->base;
    char stop[200];
    for (i = 0; i < ORIGIN_NAMES; i++) {
        snprintf(stop, sizeof stop, "FATAL unimplemented import GetProcAddress(originsdk.dll, %s)\n", origin_names[i]);
        T_TRAPS(CALL(kernel32, GetProcAddress, h, origin_names[i]), stop);
    }
    T_TRAPS(CALL(kernel32, GetProcAddress, h, (LPCVOID)(uintptr_t)1), "FATAL unimplemented import GetProcAddress(originsdk.dll, #1)\n");
    T_TRAPS(rt_module_export(O, "OriginUpdate", 0), "FATAL unimplemented import GetProcAddress(originsdk.dll, OriginUpdate)\n");
}
