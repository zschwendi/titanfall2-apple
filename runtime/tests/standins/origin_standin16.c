/* Stand-in 16 for EA's SDK (runtime/standins/originsdk/OriginShutdown.c): the call it answers,
 * its answer, its stops, and that nothing else has an answer.
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0, "no error"; nothing done): the post on issue 32 of 13 October 2026,
 *     issue 32, comment 6160020362 of the original project, row 16.
 *   - THE SHAPE (no argument, from engine+0x18471b, returning to engine+0x184720): the same post.
 *   - THE STOPS: the post's conditions.  THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.
 *     THE NAMES: origin_names.h.
 * Nothing here was taken from OriginSDK.dll, which was never read. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"
#include "origin_placeholder.h"

TEST_SETUP(standins, origin_standin16)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_16_NAME };
    origin_hand(only, 1);
}

#define STOP "stand-in 16 was called in a shape other than the one it answers"

TEST(standins, origin_standin16)
{
    Ctx *ctx = T_CTX();
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_16_NAME, 0);
    unsigned n;

    T_CASE("the shape is the one the post showed");
    T_TRUE(!strcmp(RT_STANDIN_16_NAME, "OriginShutdown"));
    T_EQ(RT_STANDIN_16_CALL_RVA, 0x18471b);
    T_EQ(RT_STANDIN_16_RETURN_RVA, 0x184720);
    op_stage();

    T_CASE("the posted call answers 0 for the placeholder user, counted and logged; nothing is written or called back");
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(op_ask(standin, 0, NULL).rax, 0);
    T_EQ(rt_standin16_answers(), 1);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 16 answered: originsdk.dll!OriginShutdown() from engine.dll+0x18471b: result 0, \"no error\""));
    T_EQ(op_cb_calls, 0);

    T_CASE("the posted call answers 0 for the user \"nobody\" too: the call carries no id and no data of anyone");
    rt_standin_user = RT_STANDIN_USER_NOBODY;
    T_EQ(op_ask(standin, 0, NULL).rax, 0);
    T_EQ(rt_standin16_answers(), 2);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_EQ(op_cb_calls, 0);
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;

    T_CASE("a call from anywhere else stops and nothing is counted: another return address, engine.dll not loaded");
    rt_standin56_test_return_rva += 5;
    T_TRAPS(op_ask(standin, 0, NULL), STOP);
    rt_standin56_test_return_rva -= 5;
    const char *const real = rt_standin56_test_module;
    rt_standin56_test_module = "no_such_module.dll";
    T_TRAPS(op_ask(standin, 0, NULL), STOP);
    rt_standin56_test_module = real;
    T_EQ(rt_standin16_answers(), 2);

    T_CASE("the answers after the first are counted and not logged: 1000 more, the same 0, no line, nothing called back");
    for (unsigned i = 0; i < 1000; i++) T_EQ(op_ask(standin, 0, NULL).rax, 0);
    T_EQ(rt_standin16_answers(), 1002);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_EQ(op_cb_calls, 0);

    T_CASE("with this row alone the 28 other imports of originsdk.dll halt by call and stop at the look-up by name");
    OP_OTHERS_HALT(ctx, RT_STANDIN_16_NAME, n);
    T_EQ(n, 28);
}
