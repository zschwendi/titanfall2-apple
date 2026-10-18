/* Stand-in 15 for EA's SDK (runtime/standins/originsdk/OriginGrantAchievement.c): the call it
 * accepts, that nothing is done with it, its stops, and that nothing else has an answer.
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0, "accepted"; nothing is done with it; any number n; every call logged
 *     and counted): the post on issue 32 of 12 October 2026,
 *     issue 32, comment 6125043112 of the original project, row 15.
 *   - THE SHAPE (OriginGrantAchievement(1, 1, n, 1, the text, 10000, engine+0x182c60, 0) from
 *     engine+0x18487b, returning to engine+0x184880): the same post.
 *   - THE STOPS: the post's conditions.  THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.
 *     THE NAMES: origin_names.h.
 * Nothing here was taken from OriginSDK.dll, which was never read. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"
#include "origin_placeholder.h"

TEST_SETUP(standins, origin_standin15)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_15_NAME };
    origin_hand(only, 1);
}

#define STOP "stand-in 15 was called in a shape other than the one it answers"

TEST(standins, origin_standin15)
{
    Ctx *ctx = T_CTX();
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_15_NAME, 0);
    static char text[] = "{331F357B-49AB-45DC-852F-4F047EADDCA3}", other[] = "{331F357B-49AB-45DC-852F-4F047EADDCA4}";
    const uint64_t T = (uint64_t)(uintptr_t)text, O = (uint64_t)(uintptr_t)other;
    unsigned n;

    T_CASE("the shape is the one the post showed");
    T_TRUE(!strcmp(RT_STANDIN_15_NAME, "OriginGrantAchievement"));
    T_EQ(RT_STANDIN_15_CALL_RVA, 0x18487b);
    T_EQ(RT_STANDIN_15_RETURN_RVA, 0x184880);
    T_EQ(RT_STANDIN_15_CALLBACK_RVA, 0x182c60);
    T_TRUE(!strcmp(RT_STANDIN_15_TEXT, text));
    const uint64_t cb = op_stage();
    const uint64_t P = RT_STANDIN_PLACEHOLDER_USER_ID;

    T_CASE("with the user \"nobody\" the call stops, in the posted shape too");
    T_TRAPS(OP_ASK(standin, 1, 1, 7, 1, T, 10000, cb, 0), "FATAL unimplemented case in originsdk.dll!OriginGrantAchievement: " STOP);
    T_TRAPS(OP_ASK(standin, 1, 1, 7, 1, T, 10000, cb, 0), "the user is nobody");
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;

    T_CASE("each other shape stops and nothing is counted: user id, persona, fourth argument, text, sixth argument, callback, eighth argument");
    T_TRAPS(OP_ASK(standin, 2, P, 7, 1, T, 10000, cb, 0), STOP);
    T_TRAPS(OP_ASK(standin, 0, P, 7, 1, T, 10000, cb, 0), STOP);
    T_TRAPS(OP_ASK(standin, P, 2, 7, 1, T, 10000, cb, 0), STOP);
    T_TRAPS(OP_ASK(standin, P, 0, 7, 1, T, 10000, cb, 0), STOP);
    T_TRAPS(OP_ASK(standin, P, P, 7, 0, T, 10000, cb, 0), STOP);
    T_TRAPS(OP_ASK(standin, P, P, 7, 2, T, 10000, cb, 0), STOP);
    T_TRAPS(OP_ASK(standin, P, P, 7, 1, O, 10000, cb, 0), STOP);
    T_TRAPS(OP_ASK(standin, P, P, 7, 1, 0, 10000, cb, 0), STOP);
    T_TRAPS(OP_ASK(standin, P, P, 7, 1, T, 10001, cb, 0), STOP);
    T_TRAPS(OP_ASK(standin, P, P, 7, 1, T, 10000, cb + 8, 0), STOP);
    T_TRAPS(OP_ASK(standin, P, P, 7, 1, T, 10000, 0, 0), STOP);
    T_TRAPS(OP_ASK(standin, P, P, 7, 1, T, 10000, cb, 1), STOP);
    rt_standin56_test_return_rva += 5;
    T_TRAPS(OP_ASK(standin, P, P, 7, 1, T, 10000, cb, 0), STOP);
    rt_standin56_test_return_rva -= 5;
    T_EQ(rt_standin15_answers(), 0);
    T_EQ(op_cb_calls, 0);

    T_CASE("the posted call answers 0, \"accepted\", counted and logged; nothing is done with it");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(OP_ASK(standin, P, P, 7, 1, T, 10000, cb, 0).rax, 0);
    T_EQ(rt_standin15_answers(), 1);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 15 answered: originsdk.dll!OriginGrantAchievement(1, 1, 7, 1, \"{331F357B-49AB-45DC-852F-4F047EADDCA3}\", 10000, engine.dll+0x182c60, 0) from engine.dll+0x18487b: result 0, accepted, AND NOTHING IS DONE WITH IT"));

    T_CASE("every call is logged and counted, not only the first: any number n, 0 and 0xffffffff included, the callback never called");
    T_EQ(OP_ASK(standin, P, P, 0, 1, T, 10000, cb, 0).rax, 0);
    T_EQ(OP_ASK(standin, P, P, 0xffffffffu, 1, T, 10000, cb, 0).rax, 0);
    T_EQ(rt_standin15_answers(), 3);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 3);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 15 answered: originsdk.dll!OriginGrantAchievement(1, 1, 4294967295, 1,"));
    T_EQ(op_cb_calls, 0);

    T_CASE("1000 more answers: each counted and logged, nothing called back");
    for (unsigned i = 0; i < 1000; i++) T_EQ(OP_ASK(standin, P, P, i, 1, T, 10000, cb, 0).rax, 0);
    T_EQ(rt_standin15_answers(), 1003);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1003);
    T_EQ(op_cb_calls, 0);

    T_CASE("with this row alone the 28 other imports of originsdk.dll halt by call and stop at the look-up by name");
    OP_OTHERS_HALT(ctx, RT_STANDIN_15_NAME, n);
    T_EQ(n, 28);
}
