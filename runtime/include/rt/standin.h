/* Stand-ins for EA's SDK (runtime/standins/).
 *
 * A stand-in is one answer to one import of originsdk.dll, written from the game's side of
 * the call only (OriginSDK.dll is never read, disassembled, lifted or run), and written
 * only after the exact answer was posted on the project's issue 32 (the owner's rule: plan
 * answer 2.1).  It supplies no identity, no entitlement, no authorisation code and nothing
 * of an account.
 *
 * STAND-INS ARE NOT SHIMS.  They are not in rt_shim_table, so in a program that does nothing
 * about them every import of originsdk.dll is the stub that halts the run with the report
 * "FATAL unimplemented import originsdk.dll!<name>".  The rows below are in the format of a
 * shim table so that a program which wants one can hand it to rt_init (RtConfig.extra_shims,
 * rt/init.h); `gate3 run` does that, row by row, each behind an environment variable that
 * leaves it out (gate3/run.c).  Each stand-in logs one line whenever it answers.
 *
 *     OriginIsProgressiveInstallationAvailableSync   "stand-in 2"   (issue 32, comment 6106041112)
 *             runtime/standins/originsdk/OriginIsProgressiveInstallationAvailableSync.c
 *     OriginGetSettingSync, setting 1 only           "stand-in 3"   (issue 32, comment 6107637088)
 *             runtime/standins/originsdk/OriginGetSettingSync.c
 *     OriginRegisterEventCallback, the engine's own callback and third argument 0 only
 *                                                    "stand-in 4"   (the same comment)
 *             runtime/standins/originsdk/OriginRegisterEventCallback.c
 *     OriginUpdate, the engine's per-frame call at engine+0x18490f only
 *                                                    "stand-in 5"   (issue 32, comment 6109085521)
 *             runtime/standins/originsdk/OriginUpdate.c
 *     OriginGetDefaultUser, the engine's per-frame call at engine+0x1849da only: 0, "nobody"
 *                                                    "stand-in 6"   (the same comment)
 *             runtime/standins/originsdk/OriginGetDefaultUser.c
 *         or, when the program selected the placeholder user (rt_standin_user below): the
 *         made-up id 1                               "stand-in 6, changed" (issue 32, comment 6114036238)
 *     OriginGetProfile, the engine's request at engine+0x183c91 only, the placeholder user only:
 *         accepted; the engine's callback is called once, from inside stand-in 5's next answer
 *                                                    "stand-in 7"   (comments 6114036238 and 6114239628)
 *             runtime/standins/originsdk/OriginGetProfile.c
 *     OriginGetDefaultPersona, the engine's call at engine+0x18483a only, the placeholder user only:
 *         the made-up persona 1 in the whole register    "stand-in 14" (issue 32, comment 6125043112)
 *             runtime/standins/originsdk/OriginGetDefaultPersona.c
 *     OriginGrantAchievement, the engine's call at engine+0x18487b only, the placeholder user only:
 *         result 0, "accepted"; nothing is done with it  "stand-in 15" (the same comment)
 *             runtime/standins/originsdk/OriginGrantAchievement.c
 *     OriginShutdown, the engine's call at engine+0x18471b only, BOTH users (no id and no data of
 *         anyone is passed): result 0, "no error"; nothing is done at all  "stand-in 16"
 *         (issue 32, comment 6160020362, the owner's decision of 13 October 2026)
 *             runtime/standins/originsdk/OriginShutdown.c
 *
 * ("Stand-in 1" is not an import: it is a call our entry point makes, gate3/run.c.)  The
 * table's rows are in the order of the names, not of the numbers.
 *
 * The table is generated from runtime/standins/<lib>/<Name>.c by the shims' generator
 * (runtime/rt.mk); the tests are runtime/tests/standins/. */
#ifndef RT_STANDIN_H
#define RT_STANDIN_H

#include <stdbool.h>
#include <stdint.h>

#include "rt/module.h"
#include "rt/shim.h"            /* Ctx */

extern const RtShimEntry rt_standin_table[];    /* sorted by name; a terminator row follows that is not counted */
extern const unsigned    rt_standin_count;

/* The names of the imports the stand-ins answer. */
#define RT_STANDIN_2_NAME "OriginIsProgressiveInstallationAvailableSync"
#define RT_STANDIN_3_NAME "OriginGetSettingSync"
#define RT_STANDIN_4_NAME "OriginRegisterEventCallback"
#define RT_STANDIN_5_NAME "OriginUpdate"
#define RT_STANDIN_6_NAME "OriginGetDefaultUser"

/* The one callback stand-in 4 accepts: the engine's own function, by module and rva (the
 * engine's code at +0x18386f to +0x183905 passes it nine times; never called by us). */
#define RT_STANDIN_4_CALLBACK_MODULE "engine.dll"
#define RT_STANDIN_4_CALLBACK_RVA    0x182130u

/* Registrations stand-in 4 has counted since the process started. */
unsigned rt_standin4_registrations(void);

/* NULL in every program.  The test of stand-in 4, whose runtime has no engine.dll, names its
 * own test module here in place of RT_STANDIN_4_CALLBACK_MODULE. */
extern const char *rt_standin4_test_module;

/* The one call stand-in 5 answers, by where it comes from: the engine's per-frame call at
 * engine+0x18490f (five bytes long, so it returns to +0x184914; tools/scout/flow_show.py
 * engine.dll 0x184890 --all, and the stop report of docs/handoff/gate3-run-7.md, entry 2).
 * The stand-in compares the guest's return address with the module's base + the RETURN rva. */
#define RT_STANDIN_56_CALLER_MODULE "engine.dll"
#define RT_STANDIN_5_CALL_RVA       0x18490fu
#define RT_STANDIN_5_RETURN_RVA     0x184914u
/* The same for stand-in 6: the call right behind it at engine+0x1849da, which returns to
 * +0x1849df (the stop report of the run with stand-in 5 alone, docs/handoff/gate3-run-8.md). */
#define RT_STANDIN_6_CALL_RVA       0x1849dau
#define RT_STANDIN_6_RETURN_RVA     0x1849dfu

/* Answers stand-in 5 has given since the process started.  It logs the first and counts the
 * rest (the call comes every frame). */
unsigned long long rt_standin5_answers(void);
unsigned long long rt_standin6_answers(void);       /* the same for stand-in 6 */

/* NULL and 0 in every program.  The tests of stand-ins 5 and 6, whose runtime has no
 * engine.dll and whose calls return to the test harness, name here the module and the rva
 * that stand for RT_STANDIN_56_CALLER_MODULE and the stand-in's RETURN rva. */
extern const char *rt_standin56_test_module;
extern uint32_t    rt_standin56_test_return_rva;

/* THE USER THE STAND-INS ANSWER FOR (issue 32, comment 6114036238, posted on 11 October 2026
 * after the owner's words "go for the 2nd option ... try the made up one first").  NOBODY in
 * every program unless it sets the variable before any guest code runs; `gate3 run` sets it
 * from GATE3_USER (gate3/run.c).
 *   NOBODY       stand-in 6 answers 0, "nobody is signed in" (comment 6109085521).
 *   PLACEHOLDER  a local, made-up user: stand-in 6 answers RT_STANDIN_PLACEHOLDER_USER_ID, the
 *                fixed number 1.  It is not the owner's id and is taken from nowhere.  The
 *                stand-ins from 7 on answer for this user only, except stand-in 16 (OriginShutdown),
 *                which answers both users; an import of the post that is
 *                not built yet halts the run as every other import does. */
typedef enum { RT_STANDIN_USER_NOBODY = 0, RT_STANDIN_USER_PLACEHOLDER = 1 } RtStandinUser;
extern RtStandinUser rt_standin_user;
#define RT_STANDIN_PLACEHOLDER_USER_ID 1ull

/* WHERE A CALL CAME FROM, for the stand-ins that answer one call site (5, 6 and 7 on): true
 * when the guest's return address (rsp points at it on entry: call this before any
 * GUEST_CALL) is RT_STANDIN_56_CALLER_MODULE's base + return_rva and that module is loaded.
 * In a test that named a module in rt_standin56_test_module, that module and
 * rt_standin56_test_return_rva stand for the engine and for EVERY stand-in's return rva (a
 * test's calls all return to the harness's one stub).  *ret is the return address, *engine
 * the module or NULL.  Defined in runtime/standins/originsdk/OriginUpdate.c. */
bool rt_standin_site(Ctx *ctx, uint32_t return_rva, uint64_t *ret, const RtModule **engine);

/* STAND-IN 3'S SECOND CASE (runtime/standins/originsdk/OriginGetSettingSync.c; issue 32, comment
 * 6114438848): setting 2 from the engine's call at engine+0x183ac2, which returns to +0x183ac7
 * (the stop report of docs/handoff/gate3-run-11.md, section 2b), for the placeholder user. */
#define RT_STANDIN_3_SETTING2_CALL_RVA   0x183ac2u
#define RT_STANDIN_3_SETTING2_RETURN_RVA 0x183ac7u
unsigned long long rt_standin3_setting2_answers(void);

/* STAND-IN 7: the one request it accepts, as the run of docs/handoff/gate3-run-11.md showed it
 * at its stop: OriginGetProfile(0, 10000, engine+0x184fd0, 0) from the call at engine+0x183c91,
 * which returns to +0x183c96; the placeholder user only. */
#define RT_STANDIN_7_NAME         "OriginGetProfile"
#define RT_STANDIN_7_CALL_RVA     0x183c91u
#define RT_STANDIN_7_RETURN_RVA   0x183c96u
#define RT_STANDIN_7_CALLBACK_RVA 0x184fd0u
#define RT_STANDIN_7_TIMEOUT_MS   10000u
/* THE PLACEHOLDER'S PROFILE, exactly as posted (comment 6114239628): the record the engine's
 * callback is handed, by offset.  Every other byte of its RT_STANDIN_7_RECORD_SIZE bytes is 0. */
#define RT_STANDIN_7_RECORD_SIZE  0x40u     /* the callback reads up to +0x29; the size EA's kit gives the record is not known */
#define RT_STANDIN_7_OFF_ID       0x08u     /* a 64-bit number: RT_STANDIN_PLACEHOLDER_USER_ID */
#define RT_STANDIN_7_OFF_NAME     0x10u     /* a pointer to the text RT_STANDIN_7_PROFILE_NAME */
#define RT_STANDIN_7_OFF_TEXT2    0x18u     /* a pointer to an empty text */
#define RT_STANDIN_7_OFF_COUNTRY  0x20u     /* a pointer to an empty text: no country */
#define RT_STANDIN_7_PROFILE_NAME "Pilot"   /* +0x00 (not read), +0x28 and +0x29 (two bytes about the account): 0 */
unsigned rt_standin7_requests(void);        /* requests accepted since the process started: 0 or 1 (a second one halts) */
unsigned rt_standin7_deliveries(void);      /* profiles delivered: 0 or 1 */
/* Stand-in 5 calls this inside every answer, after its own check of the call site: if a
 * request was accepted and its profile not yet delivered, the record is built in guest
 * memory and the engine's callback is called once, on this thread, as callback(0, record, 0,
 * 0).  Otherwise it does nothing. */
void rt_standin7_deliver(Ctx *ctx);
/* 0 in every program.  In a test (rt_standin56_test_module set) the rva, in that module, of
 * the guest function that stands for the engine's callback. */
extern uint32_t rt_standin7_test_callback_rva;

/* STAND-INS 8 ON: REQUESTS THAT ARE ACCEPTED AND NEVER COMPLETE (issue 32, comment
 * 6114036238), each for the placeholder user only, each answering one call site in the shape
 * the run's stop showed (docs/handoff/gate3-run-11.md), each logging its first answer and
 * counting the rest.  In a test, rt_standin7_test_callback_rva is the rva of what stands for
 * the engine's callback of any of them. */
#define RT_STANDIN_8_NAME         "OriginRequestAuthCode"
#define RT_STANDIN_8_CALL_RVA     0x18437bu
#define RT_STANDIN_8_RETURN_RVA   0x184380u
#define RT_STANDIN_8_CALLBACK_RVA 0x1857b0u
#define RT_STANDIN_8_TIMEOUT_MS   30000u
#define RT_STANDIN_8_CLIENT_TEXT  "TITANFALL2-PC-SERVER"
unsigned long long rt_standin8_answers(void);
#define RT_STANDIN_9_NAME         "OriginQueryEntitlements"
#define RT_STANDIN_9_CALL_RVA     0x183fe2u
#define RT_STANDIN_9_RETURN_RVA   0x183fe7u
#define RT_STANDIN_9_CALLBACK_RVA 0x181f60u
#define RT_STANDIN_9_TIMEOUT_MS   10000u
#define RT_STANDIN_9_GAME_TEXT    "Titanfall2PC"
unsigned long long rt_standin9_answers(void);
#define RT_STANDIN_10_NAME         "OriginQueryBlockedUsers"
#define RT_STANDIN_10_CALL_RVA     0x183cceu
#define RT_STANDIN_10_RETURN_RVA   0x183cd3u
#define RT_STANDIN_10_CALLBACK_RVA 0x183e90u
#define RT_STANDIN_10_TIMEOUT_MS   10000u
unsigned long long rt_standin10_answers(void);
#define RT_STANDIN_11_NAME         "OriginQueryFriends"
#define RT_STANDIN_11_CALL_RVA     0x183cf7u
#define RT_STANDIN_11_RETURN_RVA   0x183cfcu
#define RT_STANDIN_11_CALLBACK_RVA 0x184000u
#define RT_STANDIN_11_TIMEOUT_MS   10000u
unsigned long long rt_standin11_answers(void);
#define RT_STANDIN_13_NAME        "OriginSetPresence"
#define RT_STANDIN_13_CALL_RVA    0x184badu
#define RT_STANDIN_13_RETURN_RVA  0x184bb2u
unsigned long long rt_standin13_answers(void);
#define RT_STANDIN_12_NAME        "OriginCheckOnline"
#define RT_STANDIN_12_CALL_RVA    0x183a39u
#define RT_STANDIN_12_RETURN_RVA  0x183a3eu
unsigned long long rt_standin12_answers(void);
/* STAND-IN 14 (issue 32, comment 6125043112): OriginGetDefaultPersona() from the engine's call at
 * engine+0x18483a, which returns to +0x18483f, for the placeholder user only; no argument. */
#define RT_STANDIN_14_NAME        "OriginGetDefaultPersona"
#define RT_STANDIN_14_CALL_RVA    0x18483au
#define RT_STANDIN_14_RETURN_RVA  0x18483fu
unsigned long long rt_standin14_answers(void);
/* STAND-IN 15 (the same comment): OriginGrantAchievement(1, 1, n, 1, text, 10000, engine+0x182c60, 0)
 * from the engine's call at engine+0x18487b, which returns to +0x184880, for the placeholder user
 * only.  The text is the one the engine passes, exactly. */
#define RT_STANDIN_15_NAME          "OriginGrantAchievement"
#define RT_STANDIN_15_CALL_RVA      0x18487bu
#define RT_STANDIN_15_RETURN_RVA    0x184880u
#define RT_STANDIN_15_CALLBACK_RVA  0x182c60u
#define RT_STANDIN_15_TEXT          "{331F357B-49AB-45DC-852F-4F047EADDCA3}"
unsigned long long rt_standin15_answers(void);
/* STAND-IN 16 (issue 32, comment 6160020362): OriginShutdown() with no argument from the engine's
 * call at engine+0x18471b, which returns to +0x184720, for BOTH users (no id and no data of anyone
 * is passed, so the user is not tested).  Nothing is done; the engine ignores the result. */
#define RT_STANDIN_16_NAME        "OriginShutdown"
#define RT_STANDIN_16_CALL_RVA    0x18471bu
#define RT_STANDIN_16_RETURN_RVA  0x184720u
unsigned long long rt_standin16_answers(void);
/* (the next stand-in's constants go here) */

/* A MEASUREMENT SWITCH, NOT A WAY TO RUN (runtime/standins/originsdk/OriginGetSettingSync.c,
 * THE MEASUREMENT SWITCH).  False unless a program sets it before any guest code runs: stand-in
 * 3 then answers result 0 and writes nothing, which is NOT the posted answer. */
extern bool rt_standin3_leave_buffer;

#endif /* RT_STANDIN_H */
