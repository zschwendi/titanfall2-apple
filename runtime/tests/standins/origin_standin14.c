/* Stand-in 14 for EA's SDK (runtime/standins/originsdk/OriginGetDefaultPersona.c): the call it
 * answers, its answer, its stops, and that nothing else has an answer.
 *
 * Where the expected values come from:
 *   - THE ANSWER (the persona 1, the same number as the placeholder's id; nothing written, nothing
 *     called back): the post on issue 32 of 12 October 2026,
 *     issue 32, comment 6125043112 of the original project, row 14.
 *   - THE SHAPE (no argument, from engine+0x18483a, returning to engine+0x18483f): the same post.
 *   - THE STOPS: the post's conditions.  THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.
 *     THE NAMES: origin_names.h.
 * Nothing here was taken from OriginSDK.dll, which was never read. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"
#include "origin_placeholder.h"

TEST_SETUP(standins, origin_standin14)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_14_NAME };
    origin_hand(only, 1);
}

#define STOP "stand-in 14 was called in a shape other than the one it answers"

TEST(standins, origin_standin14)
{
    Ctx *ctx = T_CTX();
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_14_NAME, 0);
    unsigned n;

    T_CASE("the shape is the one the post showed");
    T_TRUE(!strcmp(RT_STANDIN_14_NAME, "OriginGetDefaultPersona"));
    T_EQ(RT_STANDIN_14_CALL_RVA, 0x18483a);
    T_EQ(RT_STANDIN_14_RETURN_RVA, 0x18483f);
    op_stage();

    T_CASE("with the user \"nobody\" the call stops, in the posted shape too");
    T_TRAPS(op_ask(standin, 0, NULL), "FATAL unimplemented case in originsdk.dll!OriginGetDefaultPersona: " STOP);
    T_TRAPS(op_ask(standin, 0, NULL), "the user is nobody");
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;

    T_CASE("a call from anywhere else stops and nothing is counted: another return address, engine.dll not loaded");
    rt_standin56_test_return_rva += 5;
    T_TRAPS(op_ask(standin, 0, NULL), STOP);
    rt_standin56_test_return_rva -= 5;
    const char *const real = rt_standin56_test_module;
    rt_standin56_test_module = "no_such_module.dll";
    T_TRAPS(op_ask(standin, 0, NULL), STOP);
    rt_standin56_test_module = real;
    T_EQ(rt_standin14_answers(), 0);

    T_CASE("the posted call answers 1 in the whole register, counted and logged; nothing is written or called back");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(op_ask(standin, 0, NULL).rax, 1);
    T_EQ(rt_standin14_answers(), 1);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 14 answered: originsdk.dll!OriginGetDefaultPersona() from engine.dll+0x18483a: result 1, THE PLACEHOLDER PERSONA"));
    T_EQ(op_cb_calls, 0);

    T_CASE("the answers after the first are counted and not logged: 1000 more, the same 1, no line, nothing called back");
    for (unsigned i = 0; i < 1000; i++) T_EQ(op_ask(standin, 0, NULL).rax, 1);
    T_EQ(rt_standin14_answers(), 1001);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_EQ(op_cb_calls, 0);

    T_CASE("with this row alone the 28 other imports of originsdk.dll halt by call and stop at the look-up by name");
    OP_OTHERS_HALT(ctx, RT_STANDIN_14_NAME, n);
    T_EQ(n, 28);
}
