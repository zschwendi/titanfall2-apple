/* Stand-in 8 for EA's SDK (runtime/standins/originsdk/OriginRequestAuthCode.c): the one request
 * it accepts, that it never completes, its stops, and that nothing else has an answer.
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0; never completes: the callback is never called, no code exists):
 *     the planner's post on issue 32 of 11 October 2026,
 *     issue 32, comment 6114036238 of the original project, row 8.
 *   - THE SHAPE (the user id 1, the text TITANFALL2-PC-SERVER, the callback engine+0x1857b0, 0,
 *     30000 as 32 bits, 0; returning to engine+0x184380): the stop report of run p3,
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

TEST_SETUP(standins, origin_standin8)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_8_NAME };
    origin_hand(only, 1);
}

#define STOP "stand-in 8 was called in a shape other than the one it answers"

TEST(standins, origin_standin8)
{
    Ctx *ctx = T_CTX();
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_8_NAME, 0);
    static char text[] = "TITANFALL2-PC-SERVER", other[] = "TITANFALL2-PC-CLIENT";
    const uint64_t T = (uint64_t)(uintptr_t)text;
    unsigned n;

    T_CASE("the shape is the one the run's stop showed");
    T_TRUE(!strcmp(RT_STANDIN_8_NAME, "OriginRequestAuthCode"));
    T_EQ(RT_STANDIN_8_CALL_RVA, 0x18437b);
    T_EQ(RT_STANDIN_8_RETURN_RVA, 0x184380);
    T_EQ(RT_STANDIN_8_CALLBACK_RVA, 0x1857b0);
    T_EQ(RT_STANDIN_8_TIMEOUT_MS, 30000);
    T_TRUE(!strcmp(RT_STANDIN_8_CLIENT_TEXT, text));
    const uint64_t cb = op_stage();

    T_CASE("with the user \"nobody\" the request stops, in the posted shape too");
    T_TRAPS(OP_ASK(standin, 1, T, cb, 0, 30000, 0), "FATAL unimplemented case in originsdk.dll!OriginRequestAuthCode: " STOP);
    T_TRAPS(OP_ASK(standin, 1, T, cb, 0, 30000, 0), "the user is nobody");
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;

    T_CASE("each other shape stops and nothing is counted: user id, text, callback, fourth, time, sixth, call site");
    T_TRAPS(OP_ASK(standin, 2, T, cb, 0, 30000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 0, T, cb, 0, 30000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, (uint64_t)(uintptr_t)other, cb, 0, 30000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, cb, 0, 30000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, T, cb + 8, 0, 30000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, T, 0, 0, 30000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, T, cb, 1, 30000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, T, cb, 0, 10000, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, T, cb, 0, 30000, 1), STOP);
    rt_standin56_test_return_rva += 5;
    T_TRAPS(OP_ASK(standin, 1, T, cb, 0, 30000, 0), STOP);
    rt_standin56_test_return_rva -= 5;
    T_EQ(rt_standin8_answers(), 0);

    T_CASE("the posted request: result 0, counted, logged; nothing is called back and nothing written");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(OP_ASK(standin, 1, T, cb, 0, 30000, 0).rax, 0);
    T_EQ(rt_standin8_answers(), 1);
    T_EQ(op_cb_calls, 0);
    T_TRUE(!strcmp(text, "TITANFALL2-PC-SERVER"));
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 8 answered: originsdk.dll!OriginRequestAuthCode(1, \"TITANFALL2-PC-SERVER\", engine.dll+0x1857b0, 0, 30000, 0) from engine.dll+0x18437b: result 0, accepted, AND IT NEVER COMPLETES: no authorisation code exists"));

    T_CASE("the fifth argument is 32 bits, as the engine sets it: the upper half of its slot is not compared");
    T_EQ(OP_ASK(standin, 1, T, cb, 0, UINT64_C(0xa5a5a5a500007530), 0).rax, 0);

    T_CASE("the answers after the first are counted and not logged; it never completes: 1000 more, no line, no call");
    for (unsigned i = 0; i < 1000; i++) T_EQ(OP_ASK(standin, 1, T, cb, 0, 30000, 0).rax, 0);
    T_EQ(rt_standin8_answers(), 1002);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_EQ(op_cb_calls, 0);

    T_CASE("with this row alone the 28 other imports of originsdk.dll halt by call and stop at the look-up by name");
    OP_OTHERS_HALT(ctx, RT_STANDIN_8_NAME, n);
    T_EQ(n, 28);
}
