/* Stand-in 13 for EA's SDK (runtime/standins/originsdk/OriginSetPresence.c): the call it accepts,
 * that nothing is done with the texts, its stops, and that nothing else has an answer.
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0; nothing is done with the texts): the planner's post on issue 32 of
 *     11 October 2026, issue 32, comment 6114036238 of the original project,
 *     row 13.
 *   - THE SHAPE (the user id 1, 3 or 6, three pointers each a text or NULL; returning to
 *     engine+0x184bb2): the stop report of run p7 and the engine's code at the call,
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

TEST_SETUP(standins, origin_standin13)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_13_NAME };
    origin_hand(only, 1);
}

#define STOP "stand-in 13 was called in a shape other than the one it answers"

TEST(standins, origin_standin13)
{
    Ctx *ctx = T_CTX();
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_13_NAME, 0);
    static char text[3][16];
    memset(text, 0xa5, sizeof text);
    const uint64_t A = (uint64_t)(uintptr_t)text[0], B = (uint64_t)(uintptr_t)text[1], C = (uint64_t)(uintptr_t)text[2];
    unsigned n;

    T_CASE("the shape is the one the run's stop showed");
    T_TRUE(!strcmp(RT_STANDIN_13_NAME, "OriginSetPresence"));
    T_EQ(RT_STANDIN_13_CALL_RVA, 0x184bad);
    T_EQ(RT_STANDIN_13_RETURN_RVA, 0x184bb2);
    op_stage();

    T_CASE("with the user \"nobody\" the call stops, in the posted shape too");
    T_TRAPS(OP_ASK(standin, 1, 3, A, 0, 0), "FATAL unimplemented case in originsdk.dll!OriginSetPresence: " STOP);
    T_TRAPS(OP_ASK(standin, 1, 3, A, 0, 0), "the user is nobody");
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;

    T_CASE("each other shape stops and nothing is counted: user id, a number other than 3 or 6, call site");
    T_TRAPS(OP_ASK(standin, 2, 3, A, 0, 0), STOP);
    T_TRAPS(OP_ASK(standin, 0, 3, A, 0, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 0, A, 0, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, 4, A, 0, 0), STOP);
    T_TRAPS(OP_ASK(standin, 1, UINT64_C(0x100000003), A, 0, 0), STOP);
    rt_standin56_test_return_rva += 5;
    T_TRAPS(OP_ASK(standin, 1, 3, A, 0, 0), STOP);
    rt_standin56_test_return_rva -= 5;
    T_EQ(rt_standin13_answers(), 0);

    T_CASE("the call as the run showed it (3, one text): result 0, counted, logged without the text");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(OP_ASK(standin, 1, 3, A, 0, 0).rax, 0);
    T_EQ(rt_standin13_answers(), 1);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 13 answered: originsdk.dll!OriginSetPresence(1, 3, a text, NULL, NULL) from engine.dll+0x184bad: result 0, accepted, AND NOTHING IS DONE WITH THE TEXTS"));

    T_CASE("6 and every combination of texts and NULL are accepted; the texts are not read (they have no terminator here) and not written");
    for (unsigned m = 0; m < 8; m++) {
        T_EQ(OP_ASK(standin, 1, 3, m & 1 ? A : 0, m & 2 ? B : 0, m & 4 ? C : 0).rax, 0);
        T_EQ(OP_ASK(standin, 1, 6, m & 1 ? A : 0, m & 2 ? B : 0, m & 4 ? C : 0).rax, 0);
    }
    for (unsigned i = 0; i < sizeof text; i++) T_EQ(((const uint8_t *)text)[i], 0xa5);
    T_EQ(rt_standin13_answers(), 17);

    T_CASE("the answers after the first are counted and not logged: 1000 more, no line, nothing called back");
    for (unsigned i = 0; i < 1000; i++) T_EQ(OP_ASK(standin, 1, 3, A, 0, 0).rax, 0);
    T_EQ(rt_standin13_answers(), 1017);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_EQ(op_cb_calls, 0);

    T_CASE("with this row alone the 28 other imports of originsdk.dll halt by call and stop at the look-up by name");
    OP_OTHERS_HALT(ctx, RT_STANDIN_13_NAME, n);
    T_EQ(n, 28);
}
