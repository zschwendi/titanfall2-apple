/* Stand-in 11 for EA's SDK (runtime/standins/originsdk/OriginQueryFriends.c): the one query it accepts,
 * the handle written as 0, that it never completes, its stops, and that nothing else has an
 * answer.
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0, never completed; the handle the engine asks for written as 0): the
 *     planner's post on issue 32 of 11 October 2026,
 *     issue 32, comment 6114036238 of the original project, rows 10, 11.
 *   - THE SHAPE (the user id 1, the callback engine+0x184000, 0, 10000, a pointer to a slot;
 *     returning to engine+0x183cfc): the stop report of run p6,
 *     docs/handoff/gate3-run-11.md, section 3c.  THE HANDLE'S 8 BYTES: the unit's decision
 *     (the same handoff, decisions): its width is not knowable from the engine's side.
 *   - THE STOPS for any other shape and for the user "nobody": the post's conditions.
 *   - THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.  THE NAMES: origin_names.h.
 * Nothing here was taken from OriginSDK.dll, which was never read. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"
#include "origin_placeholder.h"

TEST_SETUP(standins, origin_standin11)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_11_NAME };
    origin_hand(only, 1);
}

#define STOP "stand-in 11 was called in a shape other than the one it answers"

TEST(standins, origin_standin11)
{
    Ctx *ctx = T_CTX();
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_11_NAME, 0);
    static struct { uint64_t pre, slot, post; } box;
    const uint64_t H = (uint64_t)(uintptr_t)&box.slot;
    unsigned n;

    T_CASE("the shape is the one the run's stop showed");
    T_TRUE(!strcmp(RT_STANDIN_11_NAME, "OriginQueryFriends"));
    T_EQ(RT_STANDIN_11_CALL_RVA, 0x183cf7);
    T_EQ(RT_STANDIN_11_RETURN_RVA, 0x183cfc);
    T_EQ(RT_STANDIN_11_CALLBACK_RVA, 0x184000);
    T_EQ(RT_STANDIN_11_TIMEOUT_MS, 10000);
    const uint64_t cb = op_stage();
    box.pre = box.slot = box.post = UINT64_C(0xa5a5a5a5a5a5a5a5);

    T_CASE("with the user \"nobody\" the query stops, in the posted shape too");
    T_TRAPS(OP_ASK(standin, 1, cb, 0, 10000, H), "FATAL unimplemented case in originsdk.dll!OriginQueryFriends: " STOP);
    T_TRAPS(OP_ASK(standin, 1, cb, 0, 10000, H), "the user is nobody");
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;

    T_CASE("each other shape stops, nothing is written and nothing counted: user id, callback, third, time, a NULL handle pointer, call site");
    T_TRAPS(OP_ASK(standin, 2, cb, 0, 10000, H), STOP);
    T_TRAPS(OP_ASK(standin, 1, cb + 8, 0, 10000, H), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, 0, 10000, H), STOP);
    T_TRAPS(OP_ASK(standin, 1, cb, 1, 10000, H), STOP);
    T_TRAPS(OP_ASK(standin, 1, cb, 0, 30000, H), STOP);
    T_TRAPS(OP_ASK(standin, 1, cb, 0, 10000, 0), STOP);
    rt_standin56_test_return_rva += 5;
    T_TRAPS(OP_ASK(standin, 1, cb, 0, 10000, H), STOP);
    rt_standin56_test_return_rva -= 5;
    T_EQ(rt_standin11_answers(), 0);
    T_EQ(box.slot, UINT64_C(0xa5a5a5a5a5a5a5a5));

    T_CASE("the posted query: result 0, the handle's 8 bytes written as 0 and nothing around them, counted, logged; nothing is called back");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(OP_ASK(standin, 1, cb, 0, 10000, H).rax, 0);
    T_EQ(box.slot, 0);
    T_EQ(box.pre, UINT64_C(0xa5a5a5a5a5a5a5a5));
    T_EQ(box.post, UINT64_C(0xa5a5a5a5a5a5a5a5));
    T_EQ(rt_standin11_answers(), 1);
    T_EQ(op_cb_calls, 0);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 11 answered: originsdk.dll!OriginQueryFriends(1, engine.dll+0x184000, 0, 10000, "));
    T_TRUE(T_LOGGED(RT_CH_LOADER, "result 0, accepted, the handle written as 0 (8 bytes), AND IT NEVER COMPLETES: no list of friends exists"));

    T_CASE("the answers after the first are counted and not logged; it never completes: 1000 more, no line, no call");
    for (unsigned i = 0; i < 1000; i++) { box.slot = i + 1; T_EQ(OP_ASK(standin, 1, cb, 0, 10000, H).rax, 0); T_EQ(box.slot, 0); }
    T_EQ(rt_standin11_answers(), 1001);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_EQ(op_cb_calls, 0);

    T_CASE("with this row alone the 28 other imports of originsdk.dll halt by call and stop at the look-up by name");
    OP_OTHERS_HALT(ctx, RT_STANDIN_11_NAME, n);
    T_EQ(n, 28);
}
