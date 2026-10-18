/* Stand-in 5 for EA's SDK (rt/standin.h; runtime/standins/originsdk/OriginUpdate.c): its one
 * answer, its one stop, and that with its row alone every other import of originsdk.dll halts.
 *
 * This test's runtime is started as `gate3 run` starts its own with every other stand-in
 * switched off: the row of stand-in 5, and only that row, is handed to rt_init (TEST_SETUP).
 * A lifted module of the test imports the 29 names the game's DLLs import (origin_names.h).
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0, nothing written, nothing called back), THE CALL IT ANSWERS (the
 *     engine's per-frame call at engine+0x18490f, with no argument) and THE LOGGING (the first
 *     answer logged, the rest only counted): the planner's post on issue 32 of 11 October
 *     2026, issue 32, comment 6109085521 of the original project,
 *     and the unit's brief (swarm/briefs/gate3-run-8.md, section 1).
 *   - THE RETURN ADDRESS OF THAT CALL, engine+0x184914: the stop report of
 *     docs/handoff/gate3-run-7.md, section 5, entry 2 ("called from engine.dll+0x184914").
 *   - THE STOP for a call from anywhere else: this unit's reading of "the engine's per-frame
 *     call at engine+0x18490f" (docs/handoff/gate3-run-8.md, decisions).
 *   - THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.  THE NAMES: origin_names.h.
 * The test's runtime has no engine.dll and its calls return to the harness's own stub in
 * rttest.dll (rt_test_return): the test names that module and that rva in
 * rt_standin56_test_module and rt_standin56_test_return_rva where a call is to be "the posted
 * one".  Nothing here was taken from OriginSDK.dll, which was never read. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"

TEST_SETUP(standins, origin_standin5)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_5_NAME };
    origin_hand(only, 1);                       /* what gate3/run.c does for a row whose switch is not set */
}

/* Where a call of this test returns to: a guest function that looks at its own return address. */
static uint64_t seen_return;
GUEST_FN(where_from) { seen_return = LD64(ctx->r[4]); ctx->r[0] = 0; GUEST_RET(ctx); }

/* The call as the engine makes it: no argument. */
static RtTestRet ask(uint64_t target) {
    const char *why = NULL;
    RtTestRet r = rt_test_invoke(target, 0, NULL, &why);
    if (r.violations) rt_test_fail(__FILE__, __LINE__, "the stand-in broke the guest ABI: %s", why ? why : "?");
    return r;
}

TEST(standins, origin_standin5)
{
    Ctx *ctx = T_CTX();
    RtModule *O = rt_module_find("originsdk.dll"), *N = rt_module_find("rttest.dll");
    T_REQUIRE(O != NULL && N != NULL);
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_5_NAME, 0);
    const unsigned mine = origin_index(RT_STANDIN_5_NAME);
    rt_test_invoke(T_GUEST_ADDR(where_from), 0, NULL, NULL);
    const uint64_t harness = seen_return;       /* where every call of this test returns to: the harness's stub in rttest.dll */
    char want[300];
    unsigned i;

    T_CASE("the stand-ins' table has one row for the posted import; the call it answers returns to engine.dll+0x184914; no test module is named by default");
    unsigned rows = 0;
    for (i = 0; i < rt_standin_count; i++)
        if (!strcmp(rt_standin_table[i].name, "OriginUpdate")) {
            rows++;
            T_TRUE(!strcmp(rt_standin_table[i].lib, "originsdk"));
            T_TRUE(strstr(rt_standin_table[i].file, "runtime/standins/") != NULL);      /* not among the ordinary shims */
        }
    T_EQ(rows, 1);
    T_TRUE(!strcmp(RT_STANDIN_5_NAME, "OriginUpdate"));
    T_TRUE(!strcmp(RT_STANDIN_56_CALLER_MODULE, "engine.dll"));
    T_EQ(RT_STANDIN_5_CALL_RVA, 0x18490f);
    T_EQ(RT_STANDIN_5_RETURN_RVA, 0x184914);
    T_TRUE(rt_standin56_test_module == NULL);
    T_EQ(rt_standin56_test_return_rva, 0);
    T_TRUE(mine < ORIGIN_NAMES);
    for (i = 0; i < rt_shim_count; i++) T_TRUE(strcmp(rt_shim_table[i].lib, "originsdk") != 0);
    T_TRUE(harness >= N->base && harness - N->base < 0x10000000);
    char where[96];
    T_TRUE(strstr(rt_module_describe(harness, where, sizeof where), "rttest.dll") != NULL);

    T_CASE("while engine.dll is not loaded there is no engine+0x18490f: the call stops, and nothing is counted");
    T_TRAPS(ask(standin), "FATAL unimplemented case in originsdk.dll!OriginUpdate: stand-in 5 was called from somewhere other than the one call it answers");
    snprintf(want, sizeof want, "which returns to +0x184914): the return address is %#llx (engine.dll is not loaded)", (unsigned long long)harness);
    T_TRAPS(ask(standin), want);
    T_EQ(rt_standin5_answers(), 0);

    T_CASE("a call that returns somewhere else in the named module stops (the rva is compared, not only the module)");
    rt_standin56_test_module = "rttest.dll";
    rt_standin56_test_return_rva = (uint32_t)(harness - N->base) + 5;
    T_TRAPS(ask(standin), "stand-in 5 was called from somewhere other than the one call it answers");
    rt_standin56_test_return_rva = (uint32_t)(harness - N->base) + RT_STANDIN_5_RETURN_RVA;    /* the engine's rva, in a module that is not the engine */
    T_TRAPS(ask(standin), "stand-in 5 was called from somewhere other than the one call it answers");
    snprintf(want, sizeof want, "the return address is %#llx (rttest.dll is loaded)", (unsigned long long)harness);
    T_TRAPS(ask(standin), want);
    T_EQ(rt_standin5_answers(), 0);

    T_CASE("a call that returns to the same rva of another module stops");
    rt_standin56_test_module = "kernel32.dll";
    rt_standin56_test_return_rva = (uint32_t)(harness - N->base);
    T_TRAPS(ask(standin), "stand-in 5 was called from somewhere other than the one call it answers");
    T_EQ(rt_standin5_answers(), 0);

    /* From here on the harness's own return address stands for engine.dll+0x184914. */
    rt_standin56_test_module = "rttest.dll";
    rt_standin56_test_return_rva = (uint32_t)(harness - N->base);

    T_CASE("the posted call (no argument, from the one call site): result 0, counted");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    RtTestRet r = ask(standin);
    T_EQ(r.rax, 0);                             /* the whole register: the engine tests its low half */
    T_EQ(rt_standin5_answers(), 1);

    T_CASE("the first answer is logged, with what it is");
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 5 answered: originsdk.dll!OriginUpdate() from engine.dll+0x18490f: result 0"));
    T_TRUE(T_LOGGED(RT_CH_LOADER, "nothing written and nothing called back"));
    T_TRUE(T_LOGGED(RT_CH_LOADER, "the answers after this one are counted and not logged"));

    T_CASE("the answers after the first are counted and not logged: 1000 more, each result 0, no line");
    for (i = 0; i < 1000; i++) T_EQ(ask(standin).rax, 0);
    T_EQ(rt_standin5_answers(), 1001);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);

    T_CASE("it writes nothing: the registers that hold leftovers in the engine's call may point anywhere, and what they point at is as it was");
    struct { uint8_t pre[64]; uint8_t mid[64]; uint8_t post[64]; } box;
    memset(&box, 0xa5, sizeof box);
    const RtTestArg leftovers[4] = {            /* rcx, rdx, r8, r9: the engine sets up none of them */
        { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)box.mid } }, { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)box.mid } },
        { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)box.mid } }, { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)box.mid } },
    };
    T_EQ(rt_test_invoke(standin, 4, leftovers, NULL).rax, 0);
    for (i = 0; i < sizeof box; i++) T_EQ(((const uint8_t *)&box)[i], 0xa5);
    T_EQ(rt_standin5_answers(), 1002);

    T_CASE("a lifted module that imports all 29: one slot is the stand-in, 28 are stubs of the halting kind");
    RtModule *A = rt_module_load_a(ctx, "t_lift_a.dll", 0);
    T_REQUIRE(A != NULL);
    T_EQ(O->nshims, 1);                         /* one import of originsdk.dll has an answer in this runtime */
    T_TRUE(O->nshims == 1 && !strcmp(O->shims[0]->name, RT_STANDIN_5_NAME));
    T_EQ(LD64(A->base + ORIGIN_SLOT(mine)), standin);
    T_TRUE(standin >= O->base + RT_NATIVE_SHIM_STUBS && standin < O->base + RT_NATIVE_TRAP_STUBS);
    unsigned halting = 0;
    for (i = 0; i < ORIGIN_NAMES; i++) {
        if (i == mine) continue;
        uint64_t slot = LD64(A->base + ORIGIN_SLOT(i));
        T_EQ(slot, rt_native_import("originsdk.dll", origin_names[i], 0));
        T_TRUE(slot >= O->base + RT_NATIVE_TRAP_STUBS && slot != standin);      /* in the area of the trap stubs */
        halting++;
    }
    T_EQ(halting, 28);
    T_EQ(ask(LD64(A->base + ORIGIN_SLOT(mine))).rax, 0);        /* the module's own slot answers like the import does */

    T_CASE("the call right behind it in the engine's function halts, by name, and so does every other import of originsdk.dll");
    T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(origin_index("OriginGetDefaultUser"))), 0, NULL, NULL),
            "FATAL unimplemented import originsdk.dll!OriginGetDefaultUser\n");
    for (i = 0; i < ORIGIN_NAMES; i++)
        if (i != mine) T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(i)), 0, NULL, NULL), origin_halt_text(i));

    T_CASE("with this row alone a look-up by name gives the stand-in for its own name and stops for the 28 others");
    const HMODULE h = (HMODULE)(uintptr_t)O->base;
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, GetProcAddress, h, RT_STANDIN_5_NAME), standin);
    for (i = 0; i < ORIGIN_NAMES; i++) {
        if (i == mine) continue;
        snprintf(want, sizeof want, "FATAL unimplemented import GetProcAddress(originsdk.dll, %s)\n", origin_names[i]);
        T_TRAPS(CALL(kernel32, GetProcAddress, h, origin_names[i]), want);
    }
}
