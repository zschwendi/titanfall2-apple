/* Stand-in 3 for EA's SDK (rt/standin.h; runtime/standins/originsdk/OriginGetSettingSync.c):
 * its one answer, its stops, and that with it alone every other import of originsdk.dll halts.
 *
 * This test's runtime is started as `gate3 run` starts its own with every other stand-in
 * switched off: the row of stand-in 3, and only that row, is handed to rt_init (TEST_SETUP).
 *
 * Where the expected values come from:
 *   - THE ANSWER (result 0; the text "production" with its terminating zero, 11 bytes, written
 *     to the buffer; the size slot left as it is) and THE STOPS (any other setting number; a
 *     NULL buffer; a size under 11): the planner's post on issue 32 of 11 October 2026,
 *     issue 32, comment 6107637088 of the original project, and the
 *     unit's brief (swarm/briefs/gate3-run-7.md, section 2).  A NULL pointer to the size is a
 *     stop because the size cannot then be compared with 11 (this unit's reading).
 *   - THE OBSERVED CALL (setting 1, a 64-byte buffer of zeros, a pointer to a 64-bit slot
 *     holding 0x40): the stop report of docs/handoff/gate3-run-6.md, entry 4 (rcx=1, rdx the
 *     engine's buffer, r8 a stack slot) and the engine's instructions before the call as that
 *     hand-off's section 4 reads them (a 64-bit store of 0x40).
 *   - THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.  THE NAMES: origin_names.h.
 *   - THE MEASUREMENT SWITCH (result 0, nothing written): the same post's rejected candidate,
 *     "an empty buffer", and the brief's "measure beside it, once".
 * Nothing here was taken from OriginSDK.dll, which was never read. */
#include "origin_names.h"

TEST_SETUP(standins, origin_standin3)
{
    origin_write_importer();
    static const char *const only[] = { RT_STANDIN_3_NAME };
    origin_hand(only, 1);
}

/* The call as the engine makes it: three arguments in rcx, rdx, r8, each a whole register. */
static RtTestRet ask(uint64_t target, uint64_t setting, uint64_t buffer, uint64_t size_ptr) {
    const RtTestArg a[3] = {
        { RT_AC_U64, 8, { .u = setting } }, { RT_AC_PTR, 8, { .u = buffer } }, { RT_AC_PTR, 8, { .u = size_ptr } },
    };
    const char *why = NULL;
    RtTestRet r = rt_test_invoke(target, 3, a, &why);
    if (r.violations) rt_test_fail(__FILE__, __LINE__, "the stand-in broke the guest ABI: %s", why ? why : "?");
    return r;
}

typedef struct { uint8_t pre[64]; char buf[64]; uint8_t mid[64]; uint64_t size; uint8_t post[64]; } Box;

static void box_set(Box *b, int fill, uint64_t size) {
    memset(b, 0xa5, sizeof *b);
    memset(b->buf, fill, sizeof b->buf);
    b->size = size;
}

static void around_untouched(const Box *b) {
    for (unsigned i = 0; i < 64; i++) { T_EQ(b->pre[i], 0xa5); T_EQ(b->mid[i], 0xa5); T_EQ(b->post[i], 0xa5); }
}

#define B(p)  ((uint64_t)(uintptr_t)(p))

TEST(standins, origin_standin3)
{
    Ctx *ctx = T_CTX();
    RtModule *O = rt_module_find("originsdk.dll");
    T_REQUIRE(O != NULL);
    const unsigned mine = origin_index(RT_STANDIN_3_NAME);
    const uint64_t standin = rt_native_import("originsdk.dll", RT_STANDIN_3_NAME, 0);
    static Box box;
    unsigned i;

    T_CASE("the stand-ins' table has one row for the posted import; the measurement switch is off");
    unsigned rows = 0;
    for (i = 0; i < rt_standin_count; i++)
        if (!strcmp(rt_standin_table[i].name, "OriginGetSettingSync")) {
            rows++;
            T_TRUE(!strcmp(rt_standin_table[i].lib, "originsdk"));
            T_TRUE(strstr(rt_standin_table[i].file, "runtime/standins/") != NULL);
        }
    T_EQ(rows, 1);
    T_TRUE(!strcmp(RT_STANDIN_3_NAME, "OriginGetSettingSync"));
    T_TRUE(mine < ORIGIN_NAMES);
    T_EQ(rt_standin3_leave_buffer, false);
    for (i = 0; i < rt_shim_count; i++) T_TRUE(strcmp(rt_shim_table[i].lib, "originsdk") != 0);

    T_CASE("the observed call (setting 1, a 64-byte buffer of zeros, a pointer to a size of 0x40): result 0");
    box_set(&box, 0, 0x40);
    RtTestRet r = ask(standin, 1, B(box.buf), B(&box.size));
    T_EQ(r.rax, 0);                             /* the whole register: the engine tests its low half */

    T_CASE("the 11 bytes of \"production\" with its terminating zero are at the buffer; the rest of it, the size and everything around are as they were");
    T_EQ(memcmp(box.buf, "production", 11), 0);
    for (i = 11; i < sizeof box.buf; i++) T_EQ(box.buf[i], 0);
    T_EQ(box.size, 0x40);
    around_untouched(&box);

    T_CASE("a buffer that was not empty: exactly 11 bytes are written");
    box_set(&box, 0x5a, 0x40);
    T_EQ(ask(standin, 1, B(box.buf), B(&box.size)).rax, 0);
    T_EQ(memcmp(box.buf, "production", 11), 0);
    for (i = 11; i < sizeof box.buf; i++) T_EQ(box.buf[i], 0x5a);
    T_EQ(box.size, 0x40);
    around_untouched(&box);

    T_CASE("a size of exactly 11 is answered, and left at 11");
    box_set(&box, 0, 11);
    T_EQ(ask(standin, 1, B(box.buf), B(&box.size)).rax, 0);
    T_EQ(memcmp(box.buf, "production", 11), 0);
    T_EQ(box.buf[11], 0);
    T_EQ(box.size, 11);
    around_untouched(&box);

    T_CASE("one line is logged for each answer");
    T_TRUE(T_LOGGED(RT_CH_LOADER, "stand-in 3 answered: originsdk.dll!OriginGetSettingSync(1, "));
    T_TRUE(T_LOGGED(RT_CH_LOADER, "result 0, the 11 bytes of \"production\" written at the buffer, the size left as it is"));
    unsigned before = T_LOG_COUNT(RT_CH_LOADER);
    box_set(&box, 0, 0x40);
    T_EQ(ask(standin, 1, B(box.buf), B(&box.size)).rax, 0);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 1);
    T_EQ(ask(standin, 1, B(box.buf), B(&box.size)).rax, 0);
    T_EQ(T_LOG_COUNT(RT_CH_LOADER), before + 2);

    T_CASE("a lifted module that imports all 29: one slot is the stand-in, 28 are stubs of the halting kind");
    RtModule *A = rt_module_load_a(ctx, "t_lift_a.dll", 0);
    T_REQUIRE(A != NULL);
    T_EQ(O->nshims, 1);
    T_TRUE(O->nshims == 1 && !strcmp(O->shims[0]->name, RT_STANDIN_3_NAME));
    T_EQ(LD64(A->base + ORIGIN_SLOT(mine)), standin);
    T_TRUE(standin >= O->base + RT_NATIVE_SHIM_STUBS && standin < O->base + RT_NATIVE_TRAP_STUBS);
    for (i = 0; i < ORIGIN_NAMES; i++) {
        if (i == mine) continue;
        uint64_t slot = LD64(A->base + ORIGIN_SLOT(i));
        T_EQ(slot, rt_native_import("originsdk.dll", origin_names[i], 0));
        T_TRUE(slot >= O->base + RT_NATIVE_TRAP_STUBS && slot != standin);
    }
    box_set(&box, 0, 0x40);
    T_EQ(ask(LD64(A->base + ORIGIN_SLOT(mine)), 1, B(box.buf), B(&box.size)).rax, 0);     /* the module's own slot answers like the import */
    T_EQ(memcmp(box.buf, "production", 11), 0);

    T_CASE("every other import of originsdk.dll halts, by name (stand-in 2's and the event registration among them)");
    for (i = 0; i < ORIGIN_NAMES; i++)
        if (i != mine) T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(i)), 0, NULL, NULL), origin_halt_text(i));

    T_CASE("setting 2 and setting 3, the two the engine asks next, stop, and the report prints the three arguments");
    box_set(&box, 0, 0x100);
    char want[240];
    T_TRAPS(ask(standin, 2, B(box.buf), B(&box.size)), "FATAL unimplemented case in originsdk.dll!OriginGetSettingSync: stand-in 3 was called for setting 2 in a shape other than the one it answers");
    snprintf(want, sizeof want, "the user is nobody, the return address is");     /* setting 2 has an answer for the placeholder user from one call site only: origin_standin3_setting2.c */
    T_TRAPS(ask(standin, 2, B(box.buf), B(&box.size)), want);
    snprintf(want, sizeof want, "setting 0x3, buffer %#llx, pointer to the size %#llx\n", (unsigned long long)B(box.buf), (unsigned long long)B(&box.size));
    T_TRAPS(ask(standin, 3, B(box.buf), B(&box.size)), want);

    T_CASE("any other setting number stops: 0, 4, and 1 with a bit set above the low half");
    T_TRAPS(ask(standin, 0, B(box.buf), B(&box.size)), "(setting 1, a non-NULL buffer, a non-NULL pointer to a size of 11 or more): setting 0, buffer ");
    T_TRAPS(ask(standin, 4, B(box.buf), B(&box.size)), ": setting 0x4, buffer ");
    T_TRAPS(ask(standin, UINT64_C(0x100000001), B(box.buf), B(&box.size)), ": setting 0x100000001, buffer ");

    T_CASE("a NULL buffer stops");
    snprintf(want, sizeof want, "setting 0x1, buffer 0, pointer to the size %#llx\n", (unsigned long long)B(&box.size));
    T_TRAPS(ask(standin, 1, 0, B(&box.size)), want);

    T_CASE("a NULL pointer to the size stops");
    snprintf(want, sizeof want, "setting 0x1, buffer %#llx, pointer to the size 0\n", (unsigned long long)B(box.buf));
    T_TRAPS(ask(standin, 1, B(box.buf), 0), want);

    T_CASE("a size under 11 stops: 10 and 0");
    box.size = 10;
    T_TRAPS(ask(standin, 1, B(box.buf), B(&box.size)), "stand-in 3 was called with a size of 0xa, under the 11 bytes of its answer: setting 0x1, buffer ");
    box.size = 0;
    T_TRAPS(ask(standin, 1, B(box.buf), B(&box.size)), "stand-in 3 was called with a size of 0, under the 11 bytes of its answer");

    T_CASE("after the stops (each in a process of its own) the buffer is still empty and nothing around it moved");
    for (i = 0; i < sizeof box.buf; i++) T_EQ(box.buf[i], 0);
    T_EQ(box.size, 0);
    around_untouched(&box);

    T_CASE("THE MEASUREMENT SWITCH (off unless a program sets it): result 0, nothing written, and the log line says it is not the posted answer");
    box_set(&box, 0, 0x40);
    rt_standin3_leave_buffer = true;
    r = ask(standin, 1, B(box.buf), B(&box.size));
    rt_standin3_leave_buffer = false;
    T_EQ(r.rax, 0);
    for (i = 0; i < sizeof box.buf; i++) T_EQ(box.buf[i], 0);
    T_EQ(box.size, 0x40);
    around_untouched(&box);
    T_TRUE(T_LOGGED(RT_CH_LOADER, "result 0 and NOTHING WRITTEN, the buffer left as the engine had it: THE MEASUREMENT SWITCH IS ON, this is not the posted answer"));
    T_EQ(ask(standin, 1, B(box.buf), B(&box.size)).rax, 0);         /* off again: the posted answer */
    T_EQ(memcmp(box.buf, "production", 11), 0);
}
