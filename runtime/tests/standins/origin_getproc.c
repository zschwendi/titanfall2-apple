/* THE FENCE AROUND EA'S SDK, AT THE LOOK-UP BY NAME (finding 1 of the review of the night of
 * 11 October 2026, docs/reviews/night-2026-10-11-code-review.md): GetProcAddress on
 * originsdk.dll gives a stand-in's stub for a stand-in's name and STOPS THE RUN for every
 * other name or ordinal.  It never answers NULL: "this DLL does not export the function"
 * would be an answer about EA's kit that nobody read, and a way around the halting stubs
 * (a game that asks by name would be told "no SDK" and go on).
 *
 * This runtime is the one `gate3 run` starts by default (every stand-in handed to rt_init);
 * origin_all_halt.c has the same look-ups in a runtime with no stand-in.
 *
 * Where the expected values come from:
 *   - THE RULE: the brief of unit fix-review-night, section 1 ("a look-up by name or ordinal
 *     that finds neither a shim nor a stand-in stops the run with a report in the form of an
 *     unimplemented import (`GetProcAddress(originsdk.dll, <name>)`, the caller); it never
 *     returns NULL"), which applies the rules file's "do not return a made-up answer".
 *   - THE REPORT'S FIRST LINE: that sentence, in the form of docs/RUNTIME.md section 9.1
 *     ("FATAL unimplemented import ...").  THE CALLER: the shim-frame line of every report
 *     ("in shim kernel32.dll!GetProcAddress, called from ...", runtime/core/trap.c).
 *   - THE NAMES: origin_names.h (the import tables of engine.dll and tier0.dll).  WHICH OF
 *     THEM HAVE A STAND-IN: the posts on issue 32, as origin_standins_all.c lists them.
 * Nothing here was taken from OriginSDK.dll, which was never read: whether that DLL exports
 * a given name is exactly what this runtime does not claim to know. */
#include "origin_names.h"

static const char *const posted[] = { RT_STANDIN_3_NAME, RT_STANDIN_2_NAME, RT_STANDIN_4_NAME, RT_STANDIN_5_NAME, RT_STANDIN_6_NAME, RT_STANDIN_7_NAME, RT_STANDIN_8_NAME, RT_STANDIN_9_NAME, RT_STANDIN_10_NAME, RT_STANDIN_11_NAME, RT_STANDIN_13_NAME, RT_STANDIN_12_NAME, RT_STANDIN_14_NAME, RT_STANDIN_15_NAME, RT_STANDIN_16_NAME };
#define POSTED (sizeof posted / sizeof *posted)

static bool is_posted(const char *name) {
    for (unsigned k = 0; k < POSTED; k++) if (!strcmp(posted[k], name)) return true;
    return false;
}

static const char *stop_text(const char *what) {
    static char text[200];
    snprintf(text, sizeof text, "FATAL unimplemented import GetProcAddress(originsdk.dll, %s)\n", what);
    return text;
}

TEST_SETUP(standins, origin_getproc) { rt_test_extra_shims(rt_standin_table, rt_standin_count); }

TEST(standins, origin_getproc)
{
    RtModule *O = rt_module_find("originsdk.dll");
    T_REQUIRE(O != NULL);
    T_EQ(O->kind, RT_MOD_STUBBED);
    T_EQ(rt_standin_count, POSTED);
    const HMODULE h = (HMODULE)(uintptr_t)O->base;
    unsigned i, stops = 0;

    T_CASE("the module is found whatever the case of its name, as a guest that loads it by name finds it");
    T_TRUE(rt_module_find("OriginSDK.dll") == O && rt_module_find("ORIGINSDK.DLL") == O && rt_module_find("originsdk") == O);
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, LoadLibraryA, "OriginSDK.dll"), O->base);
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, GetModuleHandleA, "ORIGINSDK.DLL"), O->base);

    T_CASE("the stand-ins' names give the stand-ins' stubs, the ones an import slot gets, and leave last error alone");
    for (i = 0; i < POSTED; i++) {
        const uint64_t standin = rt_native_import("originsdk.dll", posted[i], 0);
        T_TRUE(standin >= O->base + RT_NATIVE_SHIM_STUBS && standin < O->base + RT_NATIVE_TRAP_STUBS);
        CALL(kernel32, SetLastError, 0x1234);
        T_EQ((uint64_t)(uintptr_t)CALL(kernel32, GetProcAddress, h, posted[i]), standin);
        T_EQ(T_LAST_ERROR(), 0x1234);
        T_EQ(rt_module_export(O, posted[i], 0), standin);
    }

    T_CASE("every other name the game's DLLs import (14) stops the run at the look-up, by name");
    for (i = 0; i < ORIGIN_NAMES; i++) {
        if (is_posted(origin_names[i])) continue;
        stops++;
        T_TRAPS(CALL(kernel32, GetProcAddress, h, origin_names[i]), stop_text(origin_names[i]));
    }
    T_EQ(stops, ORIGIN_NAMES - POSTED);
    T_EQ(stops, 14);

    T_CASE("the report names the look-up's caller and says why it is a stop");
    T_TRAPS(CALL(kernel32, GetProcAddress, h, "OriginShowInviteUISync"), "in shim kernel32.dll!GetProcAddress, called from ");
    T_TRAPS(CALL(kernel32, GetProcAddress, h, "OriginShowInviteUISync"), "never answered NULL");

    T_CASE("a name no DLL of the game imports stops too: nothing here knows what EA's DLL exports");
    T_TRAPS(CALL(kernel32, GetProcAddress, h, "NoSuchNameInAnyImportTable"), stop_text("NoSuchNameInAnyImportTable"));
    T_TRAPS(CALL(kernel32, GetProcAddress, h, ""), stop_text(""));

    T_CASE("a stand-in's name in another case is not the stand-in: it stops");
    T_TRAPS(CALL(kernel32, GetProcAddress, h, "originisprogressiveinstallationavailablesync"),
            stop_text("originisprogressiveinstallationavailablesync"));
    T_TRAPS(CALL(kernel32, GetProcAddress, h, "ORIGINGETSETTINGSYNC"), stop_text("ORIGINGETSETTINGSYNC"));
    T_TRAPS(CALL(kernel32, GetProcAddress, h, "originRegisterEventCallback"), stop_text("originRegisterEventCallback"));
    T_TRAPS(CALL(kernel32, GetProcAddress, h, "_OriginGetSettingSync"), stop_text("_OriginGetSettingSync"));
    T_TRAPS(CALL(kernel32, GetProcAddress, h, "OriginGetSettingSync@24"), stop_text("OriginGetSettingSync@24"));

    T_CASE("a look-up by ordinal stops, whatever the ordinal (no stand-in has one)");
    T_TRAPS(CALL(kernel32, GetProcAddress, h, (LPCVOID)(uintptr_t)1), stop_text("#1"));
    T_TRAPS(CALL(kernel32, GetProcAddress, h, (LPCVOID)(uintptr_t)2), stop_text("#2"));
    T_TRAPS(CALL(kernel32, GetProcAddress, h, (LPCVOID)(uintptr_t)77), stop_text("#77"));
    T_TRAPS(CALL(kernel32, GetProcAddress, h, (LPCVOID)(uintptr_t)0xFFFF), stop_text("#65535"));
    T_TRAPS(CALL(kernel32, GetProcAddress, h, NULL), stop_text("#0"));         /* a NULL name is ordinal 0 */
    for (i = 0; i < O->nshims; i++) T_EQ(O->shims[i]->ordinal, 0);

    T_CASE("the route of a delay-load helper (load the DLL by name, then ask it for the name) stops in the same way");
    T_TRAPS(CALL(kernel32, GetProcAddress, CALL(kernel32, LoadLibraryA, "OriginSDK.dll"), "OriginShowInviteUISync"), stop_text("OriginShowInviteUISync"));
    T_TRAPS(CALL(kernel32, GetProcAddress, CALL(kernel32, GetModuleHandleA, "originsdk"), "OriginShowInviteUISync"),
            stop_text("OriginShowInviteUISync"));

    T_CASE("the core's own look-up, which the shim calls, stops with no guest call behind it as well");
    T_TRAPS(rt_module_export(O, "OriginStartup", 0), stop_text("OriginStartup"));
    T_TRAPS(rt_module_export(O, NULL, 3), stop_text("#3"));

    T_CASE("a name with a control character is printed without it (the report is read by tools, line by line)");
    T_TRAPS(CALL(kernel32, GetProcAddress, h, "Origin\nFATAL runtime: game data: x"), stop_text("Origin?FATAL runtime: game data: x"));
}
