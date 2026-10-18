/* Stand-in 7 for EA's SDK (rt/standin.h; runtime/standins/originsdk/OriginGetProfile.c) and the
 * one thing stand-in 5 does since it exists: the request it accepts, the stops, the one
 * delivery of the placeholder's profile, and that nothing else has an answer.
 *
 * This test's runtime has the rows of stand-ins 5, 6 and 7 and no other (TEST_SETUP), and
 * selects the placeholder user as `gate3 run` does for GATE3_USER=placeholder.
 *
 * Where the expected values come from:
 *   - THE ANSWER, THE ONE DELIVERY AND THE RECORD (+0x00 0, +0x08 the id 1, +0x10 a pointer to
 *     "Pilot", +0x18 and +0x20 pointers to empty texts, +0x28 and +0x29 0, every other byte 0;
 *     delivered once, on the same thread, from inside stand-in 5's next answer): the
 *     planner's posts on issue 32 of 11 October 2026,
 *     issue 32, comment 6114036238 of the original project (row 7)
 *     and #issuecomment-6114239628 (the record in full).
 *   - THE SHAPE OF THE REQUEST (0, 10000, the callback engine+0x184fd0, 0, returning to
 *     engine+0x183c96): the stop report of the run with stand-in 6 alone,
 *     docs/handoff/gate3-run-11.md, section 2.
 *   - THE CALLBACK'S ARGUMENTS (its second is the record; the others are not read: 0): the
 *     same handoff, section 1.  THE STOPS for any other shape and for a second request: the
 *     posts' condition "any other shape is a stop" and "called once".
 *   - THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.  THE NAMES: origin_names.h.
 * The test's runtime has no engine.dll: rttest.dll and the harness's return stub stand for the
 * engine and for each stand-in's return address (rt_standin56_test_module and
 * rt_standin56_test_return_rva), and a guest function of this test for the engine's callback
 * (rt_standin7_test_callback_rva).  Nothing here was taken from OriginSDK.dll, which was never
 * read. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "rt/standin.h"
#include "origin_names.h"

TEST_SETUP(standins, origin_standin7)
{
    origin_write_importer();
    static const char *const three[] = { RT_STANDIN_5_NAME, RT_STANDIN_6_NAME, RT_STANDIN_7_NAME };
    origin_hand(three, 3);
}

static uint64_t seen_return;
GUEST_FN(where_from) { seen_return = LD64(ctx->r[4]); ctx->r[0] = 0; GUEST_RET(ctx); }

/* What stands for the engine's profile callback: it keeps what it was handed. */
static unsigned cb_calls;
static uint64_t cb_arg[4];
static uint8_t  cb_record[RT_STANDIN_7_RECORD_SIZE];
static char     cb_text[3][16];
static Ctx     *cb_ctx;
GUEST_FN(profile_cb)
{
    cb_calls++;
    cb_ctx = ctx;
    cb_arg[0] = ctx->r[1]; cb_arg[1] = ctx->r[2]; cb_arg[2] = ctx->r[8]; cb_arg[3] = ctx->r[9];
    memcpy(cb_record, (const void *)(uintptr_t)ctx->r[2], sizeof cb_record);
    for (unsigned k = 0; k < 3; k++) {
        uint64_t p;
        memcpy(&p, cb_record + 0x10 + 8 * k, 8);
        snprintf(cb_text[k], sizeof cb_text[k], "%s", p ? (const char *)(uintptr_t)p : "(null)");
    }
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

static RtTestRet ask4(uint64_t target, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    const RtTestArg args[4] = { { RT_AC_U64, 8, { .u = a } }, { RT_AC_U64, 8, { .u = b } }, { RT_AC_U64, 8, { .u = c } }, { RT_AC_U64, 8, { .u = d } } };
    const char *why = NULL;
    RtTestRet r = rt_test_invoke(target, 4, args, &why);
    if (r.violations) rt_test_fail(__FILE__, __LINE__, "the stand-in broke the guest ABI: %s", why ? why : "?");
    return r;
}

TEST(standins, origin_standin7)
{
    Ctx *ctx = T_CTX();
    RtModule *O = rt_module_find("originsdk.dll"), *N = rt_module_find("rttest.dll");
    T_REQUIRE(O != NULL && N != NULL);
    const uint64_t profile = rt_native_import("originsdk.dll", RT_STANDIN_7_NAME, 0);
    const uint64_t update = rt_native_import("originsdk.dll", RT_STANDIN_5_NAME, 0);
    rt_test_invoke(T_GUEST_ADDR(where_from), 0, NULL, NULL);
    const uint64_t harness = seen_return;
    const uint64_t cb = T_GUEST_ADDR(profile_cb);
    T_REQUIRE(cb > N->base && cb - N->base < 0x10000000);
    char want[300];
    unsigned i;

    T_CASE("the table has one row for the posted import; the request's shape is the one the run's stop showed");
    unsigned rows = 0;
    for (i = 0; i < rt_standin_count; i++)
        if (!strcmp(rt_standin_table[i].name, "OriginGetProfile")) { rows++; T_TRUE(strstr(rt_standin_table[i].file, "runtime/standins/originsdk/") != NULL); }
    T_EQ(rows, 1);
    T_EQ(RT_STANDIN_7_CALL_RVA, 0x183c91);
    T_EQ(RT_STANDIN_7_RETURN_RVA, 0x183c96);
    T_EQ(RT_STANDIN_7_CALLBACK_RVA, 0x184fd0);
    T_EQ(RT_STANDIN_7_TIMEOUT_MS, 10000);
    T_EQ(rt_standin7_test_callback_rva, 0);
    T_EQ(rt_standin_user, RT_STANDIN_USER_NOBODY);

    /* From here on rttest.dll stands for the engine, the harness's stub for each return address, profile_cb for the callback. */
    rt_standin56_test_module = "rttest.dll";
    rt_standin56_test_return_rva = (uint32_t)(harness - N->base);
    rt_standin7_test_callback_rva = (uint32_t)(cb - N->base);

    T_CASE("with the user \"nobody\" the request stops, in the posted shape too");
    T_TRAPS(ask4(profile, 0, 10000, cb, 0), "FATAL unimplemented case in originsdk.dll!OriginGetProfile: stand-in 7 was called in a shape other than the one it answers");
    T_TRAPS(ask4(profile, 0, 10000, cb, 0), "the user is nobody");
    rt_standin_user = RT_STANDIN_USER_PLACEHOLDER;

    T_CASE("stand-in 5 delivers nothing while no request was accepted");
    T_EQ(rt_test_invoke(update, 0, NULL, NULL).rax, 0);
    T_EQ(cb_calls, 0);
    T_EQ(rt_standin7_deliveries(), 0);

    T_CASE("each other shape stops and nothing is accepted: another first argument, another time, another callback, another fourth argument, another call site");
    T_TRAPS(ask4(profile, 1, 10000, cb, 0), "stand-in 7 was called in a shape other than the one it answers");
    T_TRAPS(ask4(profile, 0, 30000, cb, 0), "stand-in 7 was called in a shape other than the one it answers");
    T_TRAPS(ask4(profile, 0, 0x100002710ull, cb, 0), "stand-in 7 was called in a shape other than the one it answers");
    T_TRAPS(ask4(profile, 0, 10000, cb + 8, 0), "stand-in 7 was called in a shape other than the one it answers");
    T_TRAPS(ask4(profile, 0, 10000, 0, 0), "stand-in 7 was called in a shape other than the one it answers");
    T_TRAPS(ask4(profile, 0, 10000, cb, 1), "stand-in 7 was called in a shape other than the one it answers");
    rt_standin56_test_return_rva += 5;
    T_TRAPS(ask4(profile, 0, 10000, cb, 0), "stand-in 7 was called in a shape other than the one it answers");
    rt_standin56_test_return_rva -= 5;
    T_EQ(rt_standin7_requests(), 0);

    T_CASE("the posted request: result 0, accepted and logged; the call itself calls nothing back");
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    T_EQ(ask4(profile, 0, 10000, cb, 0).rax, 0);
    T_EQ(rt_standin7_requests(), 1);
    T_EQ(cb_calls, 0);
    T_EQ(rt_standin7_deliveries(), 0);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 7 answered: originsdk.dll!OriginGetProfile(0, 10000, engine.dll+0x184fd0, 0) from engine.dll+0x183c91: result 0, the request accepted"));

    T_CASE("a second request stops");
    T_TRAPS(ask4(profile, 0, 10000, cb, 0), "requests accepted before this one: 1");

    T_CASE("stand-in 5's next answer delivers the profile once: callback(0, record, 0, 0) on the same thread, and stand-in 5 still answers 0");
    T_EQ(rt_test_invoke(update, 0, NULL, NULL).rax, 0);
    T_EQ(cb_calls, 1);
    T_EQ(rt_standin7_deliveries(), 1);
    T_TRUE(cb_ctx == ctx);
    T_EQ(cb_arg[0], 0);
    T_TRUE(cb_arg[1] != 0);
    T_EQ(cb_arg[2], 0);
    T_EQ(cb_arg[3], 0);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 7 delivered: the engine's profile callback (engine.dll+0x184fd0) was called once"));

    T_CASE("the record is the posted one: +0x00 0, +0x08 the id 1, +0x10 \"Pilot\", +0x18 and +0x20 empty texts that are not null pointers, +0x28 and +0x29 0, every other byte 0");
    uint64_t q[5];
    memcpy(q, cb_record, sizeof q);
    T_EQ(q[0], 0);
    T_EQ(q[1], 1);
    T_TRUE(q[2] != 0 && q[3] != 0 && q[4] != 0);
    T_TRUE(!strcmp(cb_text[0], "Pilot"));
    T_TRUE(!strcmp(cb_text[1], ""));
    T_TRUE(!strcmp(cb_text[2], ""));
    for (i = 0x28; i < RT_STANDIN_7_RECORD_SIZE; i++) T_EQ(cb_record[i], 0);
    T_EQ(RT_STANDIN_7_OFF_ID, 0x08);
    T_EQ(RT_STANDIN_7_OFF_NAME, 0x10);
    T_EQ(RT_STANDIN_7_OFF_TEXT2, 0x18);
    T_EQ(RT_STANDIN_7_OFF_COUNTRY, 0x20);
    T_TRUE(RT_STANDIN_7_RECORD_SIZE >= 0x2a);

    T_CASE("nothing more is ever delivered: 1000 further answers of stand-in 5, no call");
    for (i = 0; i < 1000; i++) T_EQ(rt_test_invoke(update, 0, NULL, NULL).rax, 0);
    T_EQ(cb_calls, 1);
    T_EQ(rt_standin7_deliveries(), 1);

    T_CASE("with these three rows every other import of originsdk.dll halts by call and stops at the look-up by name");
    RtModule *A = rt_module_load_a(ctx, "t_lift_a.dll", 0);
    T_REQUIRE(A != NULL);
    T_EQ(O->nshims, 3);
    const HMODULE h = (HMODULE)(uintptr_t)O->base;
    unsigned halting = 0;
    for (i = 0; i < ORIGIN_NAMES; i++) {
        if (!strcmp(origin_names[i], RT_STANDIN_5_NAME) || !strcmp(origin_names[i], RT_STANDIN_6_NAME) || !strcmp(origin_names[i], RT_STANDIN_7_NAME)) continue;
        T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(i)), 0, NULL, NULL), origin_halt_text(i));
        snprintf(want, sizeof want, "FATAL unimplemented import GetProcAddress(originsdk.dll, %s)\n", origin_names[i]);
        T_TRAPS(CALL(kernel32, GetProcAddress, h, origin_names[i]), want);
        halting++;
    }
    T_EQ(halting, 26);
}
