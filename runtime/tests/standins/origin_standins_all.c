/* The stand-ins for EA's SDK all together, as `gate3 run` starts by default (every row of
 * rt_standin_table handed to rt_init): EXACTLY the posted imports have an answer, and every
 * other import of originsdk.dll that the game's DLLs have halts the run and names itself.
 *
 * Where the expected values come from:
 *   - WHICH IMPORTS HAVE A STAND-IN: the two posts on issue 32 of 11 October 2026
 *     (comment 6106041112: OriginIsProgressiveInstallationAvailableSync; comment 6107637088:
 *     OriginGetSettingSync and OriginRegisterEventCallback; comment 6109085521: OriginUpdate and
 *     OriginGetDefaultUser; comments 6114036238 and 6114239628: OriginGetProfile, for the
 *     placeholder user, which this test does not select: handed its row, it would halt; the
 *     first of those two also has OriginRequestAuthCode and the rows after it)
 *     and the briefs of gate3-run-5, gate3-run-7 and gate3-run-8.  A row
 *     that is not in the list `posted` below fails this test: adding a stand-in means
 *     posting it first and then adding it here.
 *   - THE HALTING STUB'S REPORT: docs/RUNTIME.md section 9.1.  THE NAMES: origin_names.h.
 * Each stand-in's own answer and stops are in origin_standin<N>.c.  Nothing here was taken
 * from OriginSDK.dll, which was never read. */
#include "origin_names.h"

static const char *const posted[] = {           /* in the order of the names, as the table is */
    "OriginCheckOnline",
    "OriginGetDefaultPersona",
    "OriginGetDefaultUser",
    "OriginGetProfile",
    "OriginGetSettingSync",
    "OriginGrantAchievement",
    "OriginIsProgressiveInstallationAvailableSync",
    "OriginQueryBlockedUsers",
    "OriginQueryEntitlements",
    "OriginQueryFriends",
    "OriginRegisterEventCallback",
    "OriginRequestAuthCode",
    "OriginSetPresence",
    "OriginShutdown",
    "OriginUpdate",
};
#define POSTED (sizeof posted / sizeof *posted)

TEST_SETUP(standins, origin_standins_all)
{
    origin_write_importer();
    rt_test_extra_shims(rt_standin_table, rt_standin_count);    /* what gate3/run.c does when no switch is set */
}

static bool is_posted(const char *name) {
    for (unsigned k = 0; k < POSTED; k++) if (!strcmp(posted[k], name)) return true;
    return false;
}

TEST(standins, origin_standins_all)
{
    Ctx *ctx = T_CTX();
    RtModule *O = rt_module_find("originsdk.dll");
    T_REQUIRE(O != NULL);
    unsigned i;

    T_CASE("the stand-ins' table is exactly the posted imports, each of originsdk and from runtime/standins/, and the runtime's own shim table has none of originsdk");
    T_EQ(rt_standin_count, POSTED);
    for (i = 0; i < rt_standin_count && i < POSTED; i++) {
        T_TRUE(!strcmp(rt_standin_table[i].lib, "originsdk"));
        T_TRUE(!strcmp(rt_standin_table[i].name, posted[i]));
        T_TRUE(strstr(rt_standin_table[i].file, "runtime/standins/originsdk/") != NULL);
        T_TRUE(origin_index(posted[i]) < ORIGIN_NAMES);
    }
    for (i = 0; i < rt_shim_count; i++) T_TRUE(strcmp(rt_shim_table[i].lib, "originsdk") != 0);
    T_TRUE(is_posted(RT_STANDIN_2_NAME) && is_posted(RT_STANDIN_3_NAME) && is_posted(RT_STANDIN_4_NAME) && is_posted(RT_STANDIN_5_NAME));
    T_TRUE(is_posted(RT_STANDIN_6_NAME) && is_posted(RT_STANDIN_7_NAME) && is_posted(RT_STANDIN_8_NAME) && is_posted(RT_STANDIN_9_NAME) && is_posted(RT_STANDIN_10_NAME) && is_posted(RT_STANDIN_11_NAME) && is_posted(RT_STANDIN_13_NAME) && is_posted(RT_STANDIN_12_NAME) && is_posted(RT_STANDIN_14_NAME) && is_posted(RT_STANDIN_15_NAME) && is_posted(RT_STANDIN_16_NAME));
    T_EQ(POSTED, 15);                            /* the answered imports (7 on: for the placeholder user only); the others halt */
    T_EQ(ORIGIN_NAMES - POSTED, 14);
    T_EQ(ORIGIN_NAMES, 29);

    T_CASE("a lifted module that imports all 29: the posted ones are answered, the others are stubs of the halting kind");
    RtModule *A = rt_module_load_a(ctx, "t_lift_a.dll", 0);
    T_REQUIRE(A != NULL);
    T_EQ(O->nshims, POSTED);
    unsigned answered = 0, halting = 0;
    for (i = 0; i < ORIGIN_NAMES; i++) {
        uint64_t slot = LD64(A->base + ORIGIN_SLOT(i));
        T_EQ(slot, rt_native_import("originsdk.dll", origin_names[i], 0));
        if (is_posted(origin_names[i])) {
            answered++;
            T_TRUE(slot >= O->base + RT_NATIVE_SHIM_STUBS && slot < O->base + RT_NATIVE_TRAP_STUBS);
        } else {
            halting++;
            T_TRUE(slot >= O->base + RT_NATIVE_TRAP_STUBS);
        }
    }
    T_EQ(answered, POSTED);
    T_EQ(halting, ORIGIN_NAMES - POSTED);

    T_CASE("every import of originsdk.dll that was not posted halts, by name");
    for (i = 0; i < ORIGIN_NAMES; i++)
        if (!is_posted(origin_names[i])) T_TRAPS(rt_test_invoke(LD64(A->base + ORIGIN_SLOT(i)), 0, NULL, NULL), origin_halt_text(i));
}
