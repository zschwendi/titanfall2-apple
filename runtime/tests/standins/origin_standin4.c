/* Stand-in 4 for EA's SDK (rt/standin.h; runtime/standins/originsdk/OriginRegisterEventCallback.c):
 * its one answer, its stops, and that with it alone every other import of originsdk.dll halts.
 *
 * This test's runtime is started as `gate3 run` starts its own with every other stand-in
 * switched off: the row of stand-in 4, and only that row, is handed to rt_init (TEST_SETUP).
 * It has no engine.dll: the test's own module t_lift_b.dll is named in rt_standin4_test_module
 * and stands for it, so "the engine's callback" is that module's base + 0x182130 (an address
 * nothing is at and nothing calls: the stand-in compares it and never calls it).
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0, the registration counted, nothing ever called back) and THE STOPS
 *     (any other callback address; a third argument that is not 0): the planner's post on
 *     issue 32 of 11 October 2026,
 *     issue 32, comment 6107637088 of the original project, and the
 *     unit's brief (swarm/briefs/gate3-run-7.md, section 2).
 *   - THE OBSERVED CALL (event number 1, the callback engine+0x182130, third argument 0): the
 *     stop report of the run with stand-in 3 alone, docs/handoff/gate3-run-7.md section 3
 *     (rcx=1, rdx=engine+0x182130, r8=0, called from engine+0x183874).
 *   - THE NINE EVENT NUMBERS (1, 0xb, 2, 9, 0, 0x10, 8, 5, 4): the engine's own instructions
 *     before its nine calls, engine+0x18385b to +0x183905 (tools/scout/flow_show.py).
 *   - "NOTHING IS EVER CALLED BACK" is not something a test can show for all time.  What
 *     holds it is the stand-in's source: it has no GUEST_CALL and keeps no address, its whole
 *     state is the count.  The test gives it a callback address at which there is no code (the
 *     test module's image ends long before +0x182130): a call there would be a fault, and
 *     there is none.
 *   - THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.  THE NAMES: origin_names.h.
 * Nothing here was taken from OriginSDK.dll, which was never read. */
#include "origin_names.h"

#define ENGINE_STAND "t_lift_b.dll"

TEST_SETUP(standins, origin_standin4)
{
    origin_write_importer();
    RtTestPe *b = rt_test_pe_new();
    rt_test_pe_write(b, ENGINE_STAND);
    static const char *const only[] = { RT_STANDIN_4_NAME };
    origin_hand(only, 1);
}

/* The call as the engine makes it: three arguments in rcx, rdx, r8, each a whole register. */
static RtTestRet ask(uint64_t target, uint64_t event, uint64_t callback, uint64_t third) {
    const RtTestArg a[3] = {
        { RT_AC_U64, 8, { .u = event } }, { RT_AC_U64, 8, { .u = callback } }, { RT_AC_U64, 8, { .u = third } },
    };
    const char *why = NULL;
    RtTestRet r = rt_test_invoke(target, 3, a, &why);
    if (r.violations) rt_test_fail(__FILE__, __LINE__, "the stand-in broke the guest ABI: %s", why ? why : "?");
    return r;
}

TEST(standins, origin_standin4)
{
    Ctx *ctx = T_CTX();
    RtModule *O = rt_module_find("originsdk.dll");
    T_REQUIRE(O != NULL);
    const unsigned mine = origin_index(RT_STANDIN_4_NAME);
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_4_NAME, 0);
    static const uint64_t events[9] = { 1, 0xb, 2, 9, 0, 0x10, 8, 5, 4 };
    char want[300];
    unsigned i;

    T_CASE("the stand-ins' table has one row for the posted import; the callback it accepts is engine.dll+0x182130; no test module is named by default");
    unsigned rows = 0;
    for (i = 0; i < rt_standin_count; i++)
        if (!strcmp(rt_standin_table[i].name, "OriginRegisterEventCallback")) {
            rows++;
            T_TRUE(!strcmp(rt_standin_table[i].lib, "originsdk"));
            T_TRUE(strstr(rt_standin_table[i].file, "runtime/standins/") != NULL);
        }
    T_EQ(rows, 1);
    T_TRUE(!strcmp(RT_STANDIN_4_NAME, "OriginRegisterEventCallback"));
    T_TRUE(!strcmp(RT_STANDIN_4_CALLBACK_MODULE, "engine.dll"));
    T_EQ(RT_STANDIN_4_CALLBACK_RVA, 0x182130);
    T_TRUE(rt_standin4_test_module == NULL);
    T_TRUE(mine < ORIGIN_NAMES);
    for (i = 0; i < rt_shim_count; i++) T_TRUE(strcmp(rt_shim_table[i].lib, "originsdk") != 0);

    T_CASE("while engine.dll is not loaded there is no engine+0x182130: the call stops");
    T_TRAPS(ask(standin, 1, 0x182130, 0), "FATAL unimplemented case in originsdk.dll!OriginRegisterEventCallback: stand-in 4 was called with a shape other than the one it answers");
    T_TRAPS(ask(standin, 1, 0x182130, 0), "event 0x1, callback 0x182130, third 0 (engine.dll is not loaded)");

    RtModule *E = rt_module_load_a(ctx, ENGINE_STAND, 0);       /* stands for engine.dll in this test */
    T_REQUIRE(E != NULL);
    rt_standin4_test_module = ENGINE_STAND;
    const uint64_t callback = E->base + 0x182130;

    T_CASE("the observed call (event 1, the engine's callback, third 0): result 0, and the registration is counted");
    T_EQ(rt_standin4_registrations(), 0);
    RtTestRet r = ask(standin, 1, callback, 0);
    T_EQ(r.rax, 0);
    T_EQ(rt_standin4_registrations(), 1);

    T_CASE("one line is logged for each answer, with the event number and the count");
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 4 answered: originsdk.dll!OriginRegisterEventCallback(event 0x1, engine.dll+0x182130, 0): result 0, registration number 1 counted"));
    T_TRUE(T_LOGGED(RT_CH_LOADER, "nothing is ever called back"));

    T_CASE("the engine's nine registrations in its own order: each result 0, each counted, one line each");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    for (i = 0; i < 9; i++) T_EQ(ask(standin, events[i], callback, 0).rax, 0);
    T_EQ(rt_standin4_registrations(), 10);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 9);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "OriginRegisterEventCallback(event 0x10, engine.dll+0x182130, 0): result 0, registration number 7 counted"));
    T_TRUE(T_LOGGED(RT_CH_LOADER, "OriginRegisterEventCallback(event 0x4, engine.dll+0x182130, 0): result 0, registration number 10 counted"));

    T_CASE("a lifted module that imports all 29: one slot is the stand-in, 28 are stubs of the halting kind");
    RtModule *A = rt_module_load_a(ctx, "t_lift_a.dll", 0);
    T_REQUIRE(A != NULL);
    T_EQ(O->nshims, 1);
    T_TRUE(O->nshims == 1 && !strcmp(O->shims[0]->name, RT_STANDIN_4_NAME));
    T_EQ(LD64(A->base + ORIGIN_SLOT(mine)), standin);
    T_TRUE(standin >= O->base + RT_NATIVE_SHIM_STUBS && standin < O->base + RT_NATIVE_TRAP_STUBS);
    for (i = 0; i < ORIGIN_NAMES; i++) {
        if (i == mine) continue;
        uint64_t slot = LD64(A->base + ORIGIN_SLOT(i));
        T_EQ(slot, rt_native_import("originsdk.dll", origin_names[i], 0));
        T_TRUE(slot >= O->base + RT_NATIVE_TRAP_STUBS && slot != standin);
    }
    T_EQ(ask(LD64(A->base + ORIGIN_SLOT(mine)), 2, callback, 0).rax, 0);     /* the module's own slot answers like the import */
    T_EQ(rt_standin4_registrations(), 11);

    T_CASE("every other import of originsdk.dll halts, by name (the settings call and stand-in 2's among them)");
    for (i = 0; i < ORIGIN_NAMES; i++)
        if (i != mine) T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(i)), 0, NULL, NULL), origin_halt_text(i));

    T_CASE("any other callback address stops, and the report prints the three arguments: NULL, the next byte, another function, another module");
    T_TRAPS(ask(standin, 1, 0, 0), "(any event number, the callback engine.dll+0x182130, third 0): event 0x1, callback 0, third 0 (engine.dll is loaded)");
    snprintf(want, sizeof want, "event 0x1, callback %#llx, third 0 (", (unsigned long long)(callback + 1));
    T_TRAPS(ask(standin, 1, callback + 1, 0), want);
    snprintf(want, sizeof want, "event 0xb, callback %#llx, third 0 (", (unsigned long long)(E->base + 0x182140));
    T_TRAPS(ask(standin, 0xb, E->base + 0x182140, 0), want);
    snprintf(want, sizeof want, "event 0x1, callback %#llx, third 0 (", (unsigned long long)(A->base + 0x182130));
    T_TRAPS(ask(standin, 1, A->base + 0x182130, 0), want);
    T_TRAPS(ask(standin, 1, 0x182130, 0), "event 0x1, callback 0x182130, third 0 (");      /* the rva without the base */

    T_CASE("a third argument that is not 0 stops");
    snprintf(want, sizeof want, "event 0x1, callback %#llx, third 0x1 (", (unsigned long long)callback);
    T_TRAPS(ask(standin, 1, callback, 1), want);
    snprintf(want, sizeof want, "event 0x1, callback %#llx, third 0x100000000 (", (unsigned long long)callback);
    T_TRAPS(ask(standin, 1, callback, UINT64_C(0x100000000)), want);

    T_CASE("the stops counted nothing (each was in a process of its own)");
    T_EQ(rt_standin4_registrations(), 11);
    rt_standin4_test_module = NULL;
}
