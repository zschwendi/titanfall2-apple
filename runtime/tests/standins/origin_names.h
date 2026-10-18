/* The imports of originsdk.dll that the game's DLLs have: 27 of engine.dll and 3 of tier0.dll,
 * 29 names together (OriginShutdown is in both).  Source: the two DLLs' own import tables
 * (OriginSDK.dll itself was never read).  Names only: nothing here says what
 * any of them takes or returns.
 *
 * The first is the one stand-in 2 answers; the three after it are the ones the engine's
 * function engine+0x6930 goes on to after a non-zero byte ("the three behind this one"). */
#ifndef RT_TESTS_STANDINS_ORIGIN_NAMES_H
#define RT_TESTS_STANDINS_ORIGIN_NAMES_H

#include <stdio.h>
#include <string.h>

#include "rt/standin.h"
#include "rt/test_pe.h"

#define ORIGIN_DLL "OriginSDK.dll"

static const char *const origin_names[] = {
    "OriginIsProgressiveInstallationAvailableSync",
    "OriginQueryChunkStatusSync", "OriginSetDownloadUtilizationSync", "OriginReadEnumeration",
    /* engine.dll, the other 23 */
    "OriginCheckOnline", "OriginGetErrorDescription", "OriginGetSettingSync", "OriginUpdate", "OriginShutdown",
    "OriginGetDefaultUser", "OriginGetDefaultPersona", "OriginGetProfile", "OriginRequestAuthCode",
    "OriginQueryEntitlements",
    "OriginSetPresence", "OriginQueryFriends", "OriginQueryBlockedUsers", "OriginShowFriendsProfileUISync",
    "OriginShowInviteUISync", "OriginGrantAchievement",
    "OriginQueryOffers", "OriginShowCheckoutUI", "OriginShowBrowserUI",
    "OriginRegisterEventCallback", "OriginShowIGO",
    "OriginDestroyHandle", "OriginReadEnumerationSync",
    /* tier0.dll, the two that engine.dll does not import as well */
    "OriginStartup", "OriginGetAllGameInfoSync",
};
#define ORIGIN_NAMES (sizeof origin_names / sizeof *origin_names)     /* 29 */

/* A lifted module of the test (slot a) that imports all of them, as engine.dll and tier0.dll
 * do: one descriptor, the slots at ORIGIN_SLOT(i).  For TEST_SETUP. */
#define ORIGIN_SLOT(i) (0x2020u + 8u * (unsigned)(i))
static inline void origin_write_importer(void) {
    RtTestPe *a = rt_test_pe_new();
    for (unsigned i = 0; i < ORIGIN_NAMES; i++) rt_test_pe_import(a, ORIGIN_DLL, origin_names[i], 0, ORIGIN_SLOT(i));
    rt_test_pe_write(a, "t_lift_a.dll");
}

/* The place of `name` in origin_names (ORIGIN_NAMES if it is not one of them). */
static inline unsigned origin_index(const char *name) {
    unsigned i = 0;
    while (i < ORIGIN_NAMES && strcmp(origin_names[i], name) != 0) i++;
    return i;
}

/* Hand the runtime of this test the stand-ins' rows of the `n` names, and no other: what
 * gate3/run.c does row by row, each row behind its switch (run_start).  For TEST_SETUP.  The
 * number of rows found (a test checks it is `n`). */
static inline unsigned origin_hand(const char *const *names, unsigned n) {
    static RtShimEntry rows[8];
    unsigned k = 0;
    for (unsigned j = 0; j < n && j < 8; j++)
        for (unsigned i = 0; i < rt_standin_count; i++)
            if (!strcmp(rt_standin_table[i].lib, "originsdk") && !strcmp(rt_standin_table[i].name, names[j])) rows[k++] = rt_standin_table[i];
    rt_test_extra_shims(rows, k);
    return k;
}

/* "FATAL unimplemented import originsdk.dll!<name>\n": the halting stub's report (RUNTIME.md 9.1). */
static inline const char *origin_halt_text(unsigned i) {
    static char text[160];
    snprintf(text, sizeof text, "FATAL unimplemented import originsdk.dll!%s\n", origin_names[i]);
    return text;
}

#endif
