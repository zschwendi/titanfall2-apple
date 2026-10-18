/* Stand-in 3's second case (runtime/standins/originsdk/OriginGetSettingSync.c, THE SECOND
 * CASE): setting 2 from the engine's one call, for the placeholder user: its answer, its
 * stops, and that setting 1 keeps its answer.
 *
 * This test's runtime has the row of stand-in 3 and no other (TEST_SETUP) and selects the
 * placeholder user as `gate3 run` does for GATE3_USER=placeholder.
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0, one zero byte written at the buffer, the size slot left as it is),
 *     THE CALL IT ANSWERS (setting 2 from the call at engine+0x183ac2, a non-NULL buffer, a size
 *     of at least 1) AND WHAT STAYS A STOP (setting 3, every other setting number, every other
 *     call site; setting 1 keeps "production"): the planner's post on issue 32 of 11 October
 *     2026, issue 32, comment 6114438848 of the original project, and
 *     his message to the unit of 18:14 (docs/handoff/gate3-run-11.md, section 3b).
 *   - THE RETURN ADDRESS engine+0x183ac7 AND THE SHAPE (rcx 2, a buffer, a pointer to the size
 *     0x100): the stop report of run p2, the same handoff, section 2b.
 *   - FOR THE PLACEHOLDER USER ONLY: the engine reaches the call only behind a user id (the
 *     same handoff, section 3); with "nobody" it stays the stop it was.
 *   - SETTING 1's ANSWER: the post of comment 6107637088 (origin_standin3.c has its cases).
 *   - THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.  THE NAMES: origin_names.h.
 * rttest.dll and the harness's return stub stand for the engine and the return address
 * (rt_standin56_test_module, rt_standin56_test_return_rva).  Nothing here was taken from
 * OriginSDK.dll, which was never read. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"

TEST_SETUP(standins, origin_standin3_setting2)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_3_NAME };
    origin_hand(only, 1);
}

static uint64_t seen_return;
GUEST_FN(where_from) { seen_return = LD64(ctx->r[4]); ctx->r[0] = 0; GUEST_RET(ctx); }

static RtTestRet ask(uint64_t target, uint64_t setting, uint64_t buffer, uint64_t size_ptr) {
    const RtTestArg a[3] = { { RT_AC_U64, 8, { .u = setting } }, { RT_AC_PTR, 8, { .u = buffer } }, { RT_AC_PTR, 8, { .u = size_ptr } } };
    const char *why = NULL;
    RtTestRet r = rt_test_invoke(target, 3, a, &why);
    if (r.violations) rt_test_fail(__FILE__, __LINE__, "the stand-in broke the guest ABI: %s", why ? why : "?");
    return r;
}

typedef struct { uint8_t pre[64]; char buf[256]; uint8_t mid[64]; uint64_t size; uint8_t post[64]; } Box;
static void box_set(Box *b, uint64_t size) { memset(b, 0xa5, sizeof *b); b->size = size; }
#define B(p) ((uint64_t)(uintptr_t)(p))

TEST(standins, origin_standin3_setting2)
{
    Ctx *ctx = T_CTX();
    RtModule *O = rt_module_find("originsdk.dll"), *N = rt_module_find("rttest.dll");
    T_REQUIRE(O != NULL && N != NULL);
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_3_NAME, 0);
    rt_test_invoke(T_GUEST_ADDR(where_from), 0, NULL, NULL);
    const uint64_t harness = seen_return;
    static Box box;
    char want[300];
    unsigned i;

    T_CASE("the call it answers returns to engine.dll+0x183ac7");
    T_EQ(RT_STANDIN_3_SETTING2_CALL_RVA, 0x183ac2);
    T_EQ(RT_STANDIN_3_SETTING2_RETURN_RVA, 0x183ac7);
    T_EQ(rt_standin3_setting2_answers(), 0);

    rt_standin56_test_module = "rttest.dll";    /* rttest.dll stands for the engine, the harness's stub for engine.dll+0x183ac7 */
    rt_standin56_test_return_rva = (uint32_t)(harness - N->base);

    T_CASE("with the user \"nobody\" setting 2 stops, from the call site too");
    box_set(&box, 0x100);
    T_TRAPS(ask(standin, 2, B(box.buf), B(&box.size)), "FATAL unimplemented case in originsdk.dll!OriginGetSettingSync: stand-in 3 was called for setting 2 in a shape other than the one it answers");
    T_TRAPS(ask(standin, 2, B(box.buf), B(&box.size)), "the user is nobody");
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;

    T_CASE("with the placeholder: another call site, a NULL buffer, a NULL pointer to the size and a size of 0 each stop, and nothing is counted");
    rt_standin56_test_return_rva += 5;
    T_TRAPS(ask(standin, 2, B(box.buf), B(&box.size)), "stand-in 3 was called for setting 2 in a shape other than the one it answers");
    rt_standin56_test_return_rva -= 5;
    T_TRAPS(ask(standin, 2, 0, B(&box.size)), "stand-in 3 was called for setting 2 in a shape other than the one it answers");
    T_TRAPS(ask(standin, 2, B(box.buf), 0), "stand-in 3 was called for setting 2 in a shape other than the one it answers");
    box.size = 0;
    T_TRAPS(ask(standin, 2, B(box.buf), B(&box.size)), "stand-in 3 was called for setting 2 in a shape other than the one it answers");
    box.size = 0x100;
    T_EQ(rt_standin3_setting2_answers(), 0);

    T_CASE("setting 3 and every other setting number but 1 stay stops, from the same call site and for the placeholder too");
    T_TRAPS(ask(standin, 3, B(box.buf), B(&box.size)), "stand-in 3 was called with a shape other than the one it answers (setting 1, ");
    T_TRAPS(ask(standin, 3, B(box.buf), B(&box.size)), ": setting 0x3, buffer ");
    T_TRAPS(ask(standin, 0, B(box.buf), B(&box.size)), ": setting 0, buffer ");
    T_TRAPS(ask(standin, 4, B(box.buf), B(&box.size)), ": setting 0x4, buffer ");
    T_TRAPS(ask(standin, UINT64_C(0x100000002), B(box.buf), B(&box.size)), ": setting 0x100000002, buffer ");

    T_CASE("the posted call (setting 2, the engine's uncleared 256-byte buffer, the size 0x100): result 0, one zero byte written, the size and everything else as it was");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(ask(standin, 2, B(box.buf), B(&box.size)).rax, 0);
    T_EQ((uint8_t)box.buf[0], 0);
    for (i = 1; i < sizeof box.buf; i++) T_EQ((uint8_t)box.buf[i], 0xa5);
    T_EQ(box.size, 0x100);
    for (i = 0; i < 64; i++) { T_EQ(box.pre[i], 0xa5); T_EQ(box.mid[i], 0xa5); T_EQ(box.post[i], 0xa5); }
    T_TRUE(strcmp(box.buf, "true") != 0);       /* what the engine compares it with: not "true", so setting 3 is not asked */
    T_EQ(rt_standin3_setting2_answers(), 1);

    T_CASE("the first answer for setting 2 is logged; 1000 more are counted with no line, each result 0 and an empty text");
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    snprintf(want, sizeof want, "stand-in 3 answered: originsdk.dll!OriginGetSettingSync(2, %#llx, size 0x100) from engine.dll+0x183ac2: result 0, an empty text (one zero byte) written at the buffer, the size left as it is", (unsigned long long)B(box.buf));
    T_TRUE(T_LOGGED(RT_CH_LOADER, want));
    for (i = 0; i < 1000; i++) { box.buf[0] = 't'; T_EQ(ask(standin, 2, B(box.buf), B(&box.size)).rax, 0); T_EQ((uint8_t)box.buf[0], 0); }
    T_EQ(rt_standin3_setting2_answers(), 1001);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);

    T_CASE("a size of 1 is enough for the one byte");
    box_set(&box, 1);
    T_EQ(ask(standin, 2, B(box.buf), B(&box.size)).rax, 0);
    T_EQ((uint8_t)box.buf[0], 0);
    T_EQ((uint8_t)box.buf[1], 0xa5);
    T_EQ(box.size, 1);

    T_CASE("setting 1 keeps its answer, for the placeholder too: result 0 and \"production\"");
    box_set(&box, 0x40);
    T_EQ(ask(standin, 1, B(box.buf), B(&box.size)).rax, 0);
    T_TRUE(!strcmp(box.buf, "production"));
    T_EQ(box.size, 0x40);
    T_EQ(rt_standin3_setting2_answers(), 1002);

    T_CASE("with this row alone the 28 other imports of originsdk.dll halt by call and stop at the look-up by name");
    RtModule *A = rt_module_load_a(ctx, "t_lift_a.dll", 0);
    T_REQUIRE(A != NULL);
    T_EQ(O->nshims, 1);
    const HMODULE h = (HMODULE)(uintptr_t)O->base;
    unsigned halting = 0;
    for (i = 0; i < ORIGIN_NAMES; i++) {
        if (!strcmp(origin_names[i], RT_STANDIN_3_NAME)) continue;
        T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(i)), 0, NULL, NULL), origin_halt_text(i));
        snprintf(want, sizeof want, "FATAL unimplemented import GetProcAddress(originsdk.dll, %s)\n", origin_names[i]);
        T_TRAPS(CALL(kernel32, GetProcAddress, h, origin_names[i]), want);
        halting++;
    }
    T_EQ(halting, 28);
}
