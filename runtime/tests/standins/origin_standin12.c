/* Stand-in 12 for EA's SDK (runtime/standins/originsdk/OriginCheckOnline.c): the call it
 * answers, "not online", its stops, and that nothing else has an answer.
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0, the engine's byte left 0, "not online"): the planner's post on
 *     issue 32 of 11 October 2026,
 *     issue 32, comment 6114036238 of the original project, row 12.
 *   - THE SHAPE (one pointer to a byte the engine has set to 0; returning to engine+0x183a3e):
 *     the stop report of run p8 and the engine's code at the call,
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

TEST_SETUP(standins, origin_standin12)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_12_NAME };
    origin_hand(only, 1);
}

#define STOP "stand-in 12 was called in a shape other than the one it answers"

TEST(standins, origin_standin12)
{
    Ctx *ctx = T_CTX();
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_12_NAME, 0);
    static uint8_t box[24];
    memset(box, 0xa5, sizeof box);
    box[8] = 0;
    const uint64_t P = (uint64_t)(uintptr_t)&box[8];
    unsigned n;

    T_CASE("the shape is the one the run's stop showed");
    T_TRUE(!strcmp(RT_STANDIN_12_NAME, "OriginCheckOnline"));
    T_EQ(RT_STANDIN_12_CALL_RVA, 0x183a39);
    T_EQ(RT_STANDIN_12_RETURN_RVA, 0x183a3e);
    op_stage();

    T_CASE("with the user \"nobody\" the call stops, in the posted shape too");
    T_TRAPS(OP_ASK(standin, P), "FATAL unimplemented case in originsdk.dll!OriginCheckOnline: " STOP);
    T_TRAPS(OP_ASK(standin, P), "the user is nobody");
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;

    T_CASE("each other shape stops and nothing is counted: a NULL pointer, a byte that is not 0, another call site");
    T_TRAPS(OP_ASK(standin, 0), STOP);
    T_TRAPS(OP_ASK(standin, P + 1), "the byte is 165");
    rt_standin56_test_return_rva += 5;
    T_TRAPS(OP_ASK(standin, P), STOP);
    rt_standin56_test_return_rva -= 5;
    T_EQ(rt_standin12_answers(), 0);

    T_CASE("the posted call: result 0, the byte left 0 and nothing around it written, counted, logged");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(OP_ASK(standin, P).rax, 0);
    T_EQ(box[8], 0);
    for (unsigned i = 0; i < sizeof box; i++) if (i != 8) T_EQ(box[i], 0xa5);
    T_EQ(rt_standin12_answers(), 1);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 12 answered: originsdk.dll!OriginCheckOnline("));
    T_TRUE(T_LOGGED(RT_CH_LOADER, "from engine.dll+0x183a39: result 0 and the engine's byte left 0, \"NOT ONLINE\""));

    T_CASE("the answers after the first are counted and not logged: 1000 more, the byte 0 each time, no line, nothing called back");
    for (unsigned i = 0; i < 1000; i++) { T_EQ(OP_ASK(standin, P).rax, 0); T_EQ(box[8], 0); }
    T_EQ(rt_standin12_answers(), 1001);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_EQ(op_cb_calls, 0);

    T_CASE("with this row alone the 28 other imports of originsdk.dll halt by call and stop at the look-up by name");
    OP_OTHERS_HALT(ctx, RT_STANDIN_12_NAME, n);
    T_EQ(n, 28);
}
