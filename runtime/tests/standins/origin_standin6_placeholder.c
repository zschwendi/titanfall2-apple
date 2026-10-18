/* Stand-in 6 for EA's SDK with THE PLACEHOLDER USER selected (rt/standin.h, THE USER;
 * runtime/standins/originsdk/OriginGetDefaultUser.c): its one answer, its one stop, and that
 * nothing else has an answer because a user is selected.
 *
 * This test's runtime is started as `gate3 run` starts its own with GATE3_USER=placeholder and
 * every other stand-in switched off: the row of stand-in 6, and only that row, is handed to
 * rt_init (TEST_SETUP), and rt_standin_user is set before any call.  origin_standin6.c is the
 * same for the user "nobody" and has the cases this one does not repeat.
 *
 * Where the expected values come from:
 *   - THE ANSWER (the fixed number 1, a made-up id; nothing written, nothing called back):
 *     the planner's post on issue 32 of 11 October 2026 after the owner's decision,
 *     issue 32, comment 6114036238 of the original project, row
 *     "6, changed", and the unit's brief (swarm/briefs/gate3-run-11.md, section 1).  "The
 *     whole register": the engine tests all of `rax` (docs/handoff/gate3-run-7.md, 7.1 point 5).
 *   - THE CALL IT ANSWERS, ITS RETURN ADDRESS AND THE STOP for any other: as for "nobody"
 *     (origin_standin6.c); the post changes the number and nothing else.
 *   - WHAT STILL HALTS: the same post ("every other import of the kit still halts, by call and
 *     by name") and docs/handoff/gate3-run-11.md: none of the post's rows 7 to 13 is built.
 *   - THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.  THE NAMES: origin_names.h.
 * Nothing here was taken from OriginSDK.dll, which was never read. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"

TEST_SETUP(standins, origin_standin6_placeholder)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_6_NAME };
    origin_hand(only, 1);
}

static uint64_t seen_return;
GUEST_FN(where_from) { seen_return = LD64(ctx->r[4]); ctx->r[0] = 0; GUEST_RET(ctx); }

static RtTestRet ask(uint64_t target) {
    const char *why = NULL;
    RtTestRet r = rt_test_invoke(target, 0, NULL, &why);
    if (r.violations) rt_test_fail(__FILE__, __LINE__, "the stand-in broke the guest ABI: %s", why ? why : "?");
    return r;
}

TEST(standins, origin_standin6_placeholder)
{
    Ctx *ctx = T_CTX();
    RtModule *O = rt_module_find("originsdk.dll"), *N = rt_module_find("rttest.dll");
    T_REQUIRE(O != NULL && N != NULL);
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_6_NAME, 0);
    const unsigned mine = origin_index(RT_STANDIN_6_NAME);
    rt_test_invoke(T_GUEST_ADDR(where_from), 0, NULL, NULL);
    const uint64_t harness = seen_return;
    char want[300];
    unsigned i;

    T_CASE("the placeholder's id is the fixed number 1; no program has the placeholder unless it selects it");
    T_EQ(RT_STANDIN_PLACEHOLDER_USER_ID, 1);
    T_EQ(rt_standin_user, RT_STANDIN_USER_NOBODY);
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;      /* what gate3/run.c does for GATE3_USER=placeholder, before any guest code */

    T_CASE("with the placeholder selected the stop is the same: a call from anywhere but the one call site stops, and nothing is counted");
    T_TRAPS(ask(standin), "FATAL unimplemented case in originsdk.dll!OriginGetDefaultUser: stand-in 6 was called from somewhere other than the one call it answers");
    rt_standin56_test_module = "rttest.dll";
    rt_standin56_test_return_rva = (uint32_t)(harness - N->base) + 5;
    T_TRAPS(ask(standin), "stand-in 6 was called from somewhere other than the one call it answers");
    rt_standin56_test_module = "kernel32.dll";
    rt_standin56_test_return_rva = (uint32_t)(harness - N->base);
    T_TRAPS(ask(standin), "stand-in 6 was called from somewhere other than the one call it answers");
    T_EQ(rt_standin6_answers(), 0);

    rt_standin56_test_module = "rttest.dll";    /* from here on the harness's own return address stands for engine.dll+0x1849df */
    rt_standin56_test_return_rva = (uint32_t)(harness - N->base);

    T_CASE("the posted call with the placeholder selected: result 1 in the whole register, counted");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    RtTestRet r = ask(standin);
    T_EQ(r.rax, 1);
    T_EQ(rt_standin6_answers(), 1);

    T_CASE("the first answer is logged and says that the id is made up");
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 6 answered: originsdk.dll!OriginGetDefaultUser() from engine.dll+0x1849da: result 1, THE PLACEHOLDER USER: a made-up id, not the owner's and taken from nowhere"));
    T_TRUE(T_LOGGED(RT_CH_LOADER, "nothing written and nothing called back"));
    T_TRUE(!T_LOGGED(RT_CH_LOADER, "nobody is signed in"));

    T_CASE("the answers after the first are counted and not logged: 1000 more, each result 1, no line");
    for (i = 0; i < 1000; i++) T_EQ(ask(standin).rax, 1);
    T_EQ(rt_standin6_answers(), 1001);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);

    T_CASE("it writes nothing: what the leftover registers point at is as it was");
    struct { uint8_t pre[64]; uint8_t mid[64]; uint8_t post[64]; } box;
    memset(&box, 0xa5, sizeof box);
    const RtTestArg leftovers[4] = {
        { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)box.mid } }, { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)box.mid } },
        { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)box.mid } }, { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)box.mid } },
    };
    T_EQ(rt_test_invoke(standin, 4, leftovers, NULL).rax, 1);
    for (i = 0; i < sizeof box; i++) T_EQ(((const uint8_t *)&box)[i], 0xa5);

    T_CASE("selecting the placeholder answers no other import: the profile and the six behind it halt by call, and so do all 28");
    RtModule *A = rt_module_load_a(ctx, "t_lift_a.dll", 0);
    T_REQUIRE(A != NULL);
    T_EQ(O->nshims, 1);
    T_EQ(LD64(A->base + ORIGIN_SLOT(mine)), standin);
    static const char *const behind[] = { "OriginGetProfile", "OriginRequestAuthCode", "OriginQueryEntitlements", "OriginQueryBlockedUsers",
                                          "OriginQueryFriends", "OriginCheckOnline", "OriginSetPresence", "OriginGetDefaultPersona" };
    for (i = 0; i < sizeof behind / sizeof *behind; i++) {
        snprintf(want, sizeof want, "FATAL unimplemented import originsdk.dll!%s\n", behind[i]);
        T_REQUIRE(origin_index(behind[i]) < ORIGIN_NAMES);
        T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(origin_index(behind[i]))), 0, NULL, NULL), want);
    }
    unsigned halting = 0;
    for (i = 0; i < ORIGIN_NAMES; i++) {
        if (i == mine) continue;
        T_TRUE(LD64(A->base + ORIGIN_SLOT(i)) >= O->base + RT_NATIVE_TRAP_STUBS);
        T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(i)), 0, NULL, NULL), origin_halt_text(i));
        halting++;
    }
    T_EQ(halting, 28);

    T_CASE("and by name: the look-up gives the stand-in for its own name and stops for the 28 others");
    const HMODULE h = (HMODULE)(uintptr_t)O->base;
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, GetProcAddress, h, RT_STANDIN_6_NAME), standin);
    for (i = 0; i < ORIGIN_NAMES; i++) {
        if (i == mine) continue;
        snprintf(want, sizeof want, "FATAL unimplemented import GetProcAddress(originsdk.dll, %s)\n", origin_names[i]);
        T_TRAPS(CALL(kernel32, GetProcAddress, h, origin_names[i]), want);
    }
}
