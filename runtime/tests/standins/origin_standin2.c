/* Stand-in 2 for EA's SDK (rt/standin.h; runtime/standins/originsdk/): its one answer, and
 * that it is the only import of originsdk.dll that anything answers.
 *
 * This test's runtime is started as `gate3 run` starts its own with every other stand-in
 * switched off: the row of stand-in 2, and only that row, is handed to rt_init (TEST_SETUP).
 * A lifted module of the test imports the 29 names the game's DLLs import (origin_names.h),
 * as engine.dll does.  The table with all its rows is tests/standins/origin_standins_all.c.
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0, the byte left as it is, nothing else written) and THE STOPS (a
 *     first argument that is not 0, a NULL second, a third that is not 0xffffffff, a byte
 *     that is not zero on arrival): the planner's post on issue 32 of 11 October 2026,
 *     issue 32, comment 6106041112 of the original project, and the
 *     unit's brief (swarm/briefs/gate3-run-5.md, section 1).  The arguments of the observed
 *     call are those of the stop report in docs/handoff/gate3-run-4.md, entry 4
 *     (rcx=0, rdx a pointer into the caller's stack, r8=00000000ffffffff).
 *   - THE HALTING STUB'S REPORT, "FATAL unimplemented import <dll>!<name>": docs/RUNTIME.md
 *     section 9.1 (the same text runtime/tests/core/dispatch.c expects).
 *   - THE NAMES: origin_names.h.
 * Nothing here was taken from OriginSDK.dll, which was never read. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"

TEST_SETUP(standins, origin_standin2)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_2_NAME };
    origin_hand(only, 1);                       /* what gate3/run.c does for a row whose switch is not set */
}

/* The call as the engine makes it: three arguments in rcx, rdx, r8, each a whole register. */
static RtTestRet ask(uint64_t target, uint64_t first, uint64_t second, uint64_t third) {
    const RtTestArg a[3] = {
        { RT_AC_U64, 8, { .u = first } }, { RT_AC_PTR, 8, { .u = second } }, { RT_AC_U64, 8, { .u = third } },
    };
    const char *why = NULL;
    RtTestRet r = rt_test_invoke(target, 3, a, &why);
    if (r.violations) rt_test_fail(__FILE__, __LINE__, "the stand-in broke the guest ABI: %s", why ? why : "?");
    return r;
}

TEST(standins, origin_standin2)
{
    Ctx *ctx = T_CTX();
    RtModule *O = rt_module_find("originsdk.dll");
    T_REQUIRE(O != NULL);
    struct { uint8_t pre[64]; uint8_t byte; uint8_t post[64]; } box;
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_2_NAME, 0);
    unsigned i;

    T_CASE("the stand-ins' table has one row for the posted import, and the runtime's own shim table has none of originsdk");
    unsigned rows = 0;
    for (i = 0; i < rt_standin_count; i++)
        if (!strcmp(rt_standin_table[i].name, "OriginIsProgressiveInstallationAvailableSync")) {
            rows++;
            T_TRUE(!strcmp(rt_standin_table[i].lib, "originsdk"));
            T_TRUE(strstr(rt_standin_table[i].file, "runtime/standins/") != NULL);      /* not among the ordinary shims */
        }
    T_EQ(rows, 1);
    T_TRUE(!strcmp(RT_STANDIN_2_NAME, "OriginIsProgressiveInstallationAvailableSync"));
    for (i = 0; i < rt_shim_count; i++) T_TRUE(strcmp(rt_shim_table[i].lib, "originsdk") != 0);
    T_TRUE(!strcmp(origin_names[0], RT_STANDIN_2_NAME));
    T_EQ(ORIGIN_NAMES, 29);

    T_CASE("the observed call (first 0, a pointer to a zero byte, third 0xffffffff): result 0");
    memset(&box, 0xa5, sizeof box);
    box.byte = 0;
    RtTestRet r = ask(standin, 0, (uint64_t)(uintptr_t)&box.byte, 0xffffffffu);
    T_EQ(r.rax, 0);                             /* the whole register: the engine tests its low half */

    T_CASE("the byte and everything around it is left as it was");
    T_EQ(box.byte, 0);
    for (i = 0; i < sizeof box.pre; i++) T_EQ(box.pre[i], 0xa5);
    for (i = 0; i < sizeof box.post; i++) T_EQ(box.post[i], 0xa5);

    T_CASE("one line is logged for each answer");
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 2 answered: originsdk.dll!OriginIsProgressiveInstallationAvailableSync(0, "));
    T_TRUE(T_LOGGED(RT_CH_LOADER, "result 0, the caller's byte left at 0"));
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(ask(standin, 0, (uint64_t)(uintptr_t)&box.byte, 0xffffffffu).rax, 0);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_EQ(ask(standin, 0, (uint64_t)(uintptr_t)&box.byte, 0xffffffffu).rax, 0);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 2);
    T_EQ(box.byte, 0);

    T_CASE("a lifted module that imports all 29: one slot is the stand-in, 28 are stubs of the halting kind");
    RtModule *A = rt_module_load_a(ctx, "t_lift_a.dll", 0);
    T_REQUIRE(A != NULL);
    T_EQ(O->nshims, 1);                         /* one import of originsdk.dll has an answer in this runtime */
    T_TRUE(O->nshims == 1 && !strcmp(O->shims[0]->name, RT_STANDIN_2_NAME));
    T_EQ(LD64(A->base + ORIGIN_SLOT(0)), standin);
    T_TRUE(standin >= O->base + RT_NATIVE_SHIM_STUBS && standin < O->base + RT_NATIVE_TRAP_STUBS);
    for (i = 1; i < ORIGIN_NAMES; i++) {
        uint64_t slot = LD64(A->base + ORIGIN_SLOT(i));
        T_EQ(slot, rt_native_import("originsdk.dll", origin_names[i], 0));
        T_TRUE(slot >= O->base + RT_NATIVE_TRAP_STUBS && slot != standin);      /* in the area of the trap stubs */
    }
    /* The module's own slot answers like the import does. */
    T_EQ(ask(LD64(A->base + ORIGIN_SLOT(0)), 0, (uint64_t)(uintptr_t)&box.byte, 0xffffffffu).rax, 0);

    T_CASE("the three imports behind this one halt, by name");
    T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(1)), 0, NULL, NULL), "FATAL unimplemented import originsdk.dll!OriginQueryChunkStatusSync\n");
    T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(2)), 0, NULL, NULL), "FATAL unimplemented import originsdk.dll!OriginSetDownloadUtilizationSync\n");
    T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(3)), 0, NULL, NULL), "FATAL unimplemented import originsdk.dll!OriginReadEnumeration\n");

    T_CASE("every other import of originsdk.dll halts, by name");
    for (i = 4; i < ORIGIN_NAMES; i++)
        T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(i)), 0, NULL, NULL), origin_halt_text(i));

    T_CASE("a first argument that is not 0 stops, and the report prints the three");
    char want[200];
    snprintf(want, sizeof want, "first 0x1, second %#llx, third 0xffffffff", (unsigned long long)(uintptr_t)&box.byte);
    T_TRAPS(ask(standin, 1, (uint64_t)(uintptr_t)&box.byte, 0xffffffffu), "FATAL unimplemented case in originsdk.dll!OriginIsProgressiveInstallationAvailableSync: stand-in 2 was called with a shape other than the one it answers");
    T_TRAPS(ask(standin, 1, (uint64_t)(uintptr_t)&box.byte, 0xffffffffu), want);
    T_TRAPS(ask(standin, UINT64_C(0x100000000), (uint64_t)(uintptr_t)&box.byte, 0xffffffffu), "first 0x100000000, second ");

    T_CASE("a NULL second argument stops");
    T_TRAPS(ask(standin, 0, 0, 0xffffffffu), "a shape other than the one it answers (first 0, a non-NULL second, third 0xffffffff): first 0, second 0, third 0xffffffff");

    T_CASE("a third argument that is not 0xffffffff stops");
    snprintf(want, sizeof want, "first 0, second %#llx, third 0\n", (unsigned long long)(uintptr_t)&box.byte);
    T_TRAPS(ask(standin, 0, (uint64_t)(uintptr_t)&box.byte, 0), want);
    snprintf(want, sizeof want, "first 0, second %#llx, third 0x1388\n", (unsigned long long)(uintptr_t)&box.byte);
    T_TRAPS(ask(standin, 0, (uint64_t)(uintptr_t)&box.byte, 5000), want);
    snprintf(want, sizeof want, "first 0, second %#llx, third 0xffffffffffffffff\n", (unsigned long long)(uintptr_t)&box.byte);
    T_TRAPS(ask(standin, 0, (uint64_t)(uintptr_t)&box.byte, UINT64_MAX), want);

    T_CASE("a byte that is not zero when the call arrives stops");
    box.byte = 1;
    snprintf(want, sizeof want, "stand-in 2 was called with the byte at its second argument already 0x1, not 0");
    T_TRAPS(ask(standin, 0, (uint64_t)(uintptr_t)&box.byte, 0xffffffffu), want);
    snprintf(want, sizeof want, "first 0, second %#llx, third 0xffffffff\n", (unsigned long long)(uintptr_t)&box.byte);
    T_TRAPS(ask(standin, 0, (uint64_t)(uintptr_t)&box.byte, 0xffffffffu), want);
    box.byte = 0xff;
    T_TRAPS(ask(standin, 0, (uint64_t)(uintptr_t)&box.byte, 0xffffffffu), "already 0xff, not 0");

    T_CASE("after the stops (each in a process of its own) the byte is as the test set it and nothing around it moved");
    T_EQ(box.byte, 0xff);
    for (i = 0; i < sizeof box.pre; i++) T_EQ(box.pre[i], 0xa5);
    for (i = 0; i < sizeof box.post; i++) T_EQ(box.post[i], 0xa5);
}
