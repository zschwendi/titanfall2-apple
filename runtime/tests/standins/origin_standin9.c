/* Stand-in 9 for EA's SDK (runtime/standins/originsdk/OriginQueryEntitlements.c): the one query
 * it accepts, that it never completes, its stops, and that nothing else has an answer.
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0; never completes: no list, no entitlement claimed): the planner's
 *     post on issue 32 of 11 October 2026,
 *     issue 32, comment 6114036238 of the original project, row 9.
 *   - THE SHAPE (the user id 1, six zeros, the text Titanfall2PC, the callback engine+0x181f60,
 *     0, 10000 as 32 bits, 0; returning to engine+0x183fe7): the stop report of run p4,
 *     docs/handoff/gate3-run-11.md, section 3c.
 *   - THE STOPS for any other shape and for the user "nobody": the post's conditions.
 *   - THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.  THE NAMES: origin_names.h.
 * Nothing here was taken from OriginSDK.dll, which was never read. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"
#include "origin_placeholder.h"

TEST_SETUP(standins, origin_standin9)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_9_NAME };
    origin_hand(only, 1);
}

#define STOP "stand-in 9 was called in a shape other than the one it answers"

TEST(standins, origin_standin9)
{
    Ctx *ctx = T_CTX();
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_9_NAME, 0);
    static char text[] = "Titanfall2PC", other[] = "Titanfall2";
    const uint64_t T = (uint64_t)(uintptr_t)text;
    unsigned n;

    T_CASE("the shape is the one the run's stop showed");
    T_TRUE(!strcmp(RT_STANDIN_9_NAME, "OriginQueryEntitlements"));
    T_EQ(RT_STANDIN_9_CALL_RVA, 0x183fe2);
    T_EQ(RT_STANDIN_9_RETURN_RVA, 0x183fe7);
    T_EQ(RT_STANDIN_9_CALLBACK_RVA, 0x181f60);
    T_EQ(RT_STANDIN_9_TIMEOUT_MS, 10000);
    T_TRUE(!strcmp(RT_STANDIN_9_GAME_TEXT, text));
    const uint64_t cb = op_stage();

    T_CASE("with the user \"nobody\" the query stops, in the posted shape too");
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, T, cb, 0, 10000, 0), "FATAL unimplemented case in originsdk.dll!OriginQueryEntitlements: " STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, T, cb, 0, 10000, 0), "the user is nobody");
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;

    T_CASE("each other shape stops and nothing is counted: user id, each of the eight zeros, text, callback, time, call site");
    T_TRAPS(OP_ASK(standin, 2, 0, 0, 0, 0, 0, 0, T, cb, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 1, 0, 0, 0, 0, 0, T, cb, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 1, 0, 0, 0, 0, T, cb, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 1, 0, 0, 0, T, cb, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 1, 0, 0, T, cb, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 1, 0, T, cb, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 1, T, cb, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, T, cb, 1, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, T, cb, 0, 10000, 1), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, (uint64_t)(uintptr_t)other, cb, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, 0, cb, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, T, cb + 8, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, T, cb, 0, 30000, 0), STOP);
    rt_standin56_test_return_rva += 5;
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, T, cb, 0, 10000, 0), STOP);
    rt_standin56_test_return_rva -= 5;
    T_EQ(rt_standin9_answers(), 0);

    T_CASE("the posted query: result 0, counted, logged; nothing is called back and nothing written");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, T, cb, 0, 10000, 0).rax, 0);
    T_EQ(rt_standin9_answers(), 1);
    T_EQ(op_cb_calls, 0);
    T_TRUE(!strcmp(text, "Titanfall2PC"));
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 9 answered: originsdk.dll!OriginQueryEntitlements(1, six zeros, \"Titanfall2PC\", engine.dll+0x181f60, 0, 10000, 0) from engine.dll+0x183fe2: result 0, accepted, AND IT NEVER COMPLETES: no list and no entitlement exists, none is claimed"));

    T_CASE("the eleventh argument is 32 bits, as the engine sets it: the upper half of its slot is not compared");
    T_EQ(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, T, cb, 0, UINT64_C(0x11000002710), 0).rax, 0);

    T_CASE("the answers after the first are counted and not logged; it never completes: 1000 more, no line, no call");
    for (unsigned i = 0; i < 1000; i++) T_EQ(OP_ASK(standin, 1, 0, 0, 0, 0, 0, 0, T, cb, 0, 10000, 0).rax, 0);
    T_EQ(rt_standin9_answers(), 1002);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_EQ(op_cb_calls, 0);

    T_CASE("with this row alone the 28 other imports of originsdk.dll halt by call and stop at the look-up by name");
    OP_OTHERS_HALT(ctx, RT_STANDIN_9_NAME, n);
    T_EQ(n, 28);
}
