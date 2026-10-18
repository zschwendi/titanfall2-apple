/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's per-frame call (no argument, from engine+0x1849da): result 0, "nobody is signed in"; nothing is written and nothing is called back; every answer is counted; one line is logged for the first answer and none for the answers after it; when the program selected the placeholder user: result 1, the made-up id, in the whole register, everything else the same
 * RT-TRAPS:  a call from anywhere but engine+0x1849da (the return address is not engine.dll+0x1849df); a call while engine.dll is not loaded
 */

/* STAND-IN 6 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * It is not in the runtime's shim table (rt_shim_table): it is a row of rt_standin_table
 * (rt/standin.h), and a program has it only if it hands that row to rt_init itself.  `gate3
 * run` does, unless GATE3_NO_STANDIN_DEFAULT_USER=1 (gate3/run.c).  In every other program,
 * and for every import of originsdk.dll that has no stand-in, the import is the stub that
 * halts the run.
 *
 * What it is: the one answer, posted on issue 32 on 11 October 2026 before this file was
 * written (RT-DOC above; the owner's words of that day: "no user is fine to do, so go for
 * that"), to the engine's call
 *
 *     originsdk.dll!OriginGetDefaultUser()
 *
 * made at engine+0x1849da on the main thread, right behind the call stand-in 5 answers, in
 * the engine's per-frame function for EA's kit (engine+0x184890), every frame.  The answer
 * is: RESULT 0, "NOBODY IS SIGNED IN".  IT NAMES NO USER AND SUPPLIES NO ID: no identity, no
 * user or persona id, no entitlement, no authorisation code, nothing of an account.  The
 * result is the whole 64-bit register, and all of it is 0.  It writes nothing and calls
 * nothing back.
 *
 * What the engine does with it, by its own code (docs/handoff/gate3-run-7.md, section 7.1,
 * point 5): while the engine has no user id, a result of 0 ends the per-frame function for
 * that frame, and it asks again in the next frame.  None of its account calls is reached: no
 * profile, no authorisation code, no entitlement query, no friends or blocked users, no
 * presence, no online check.  Each of those imports is a halting stub in any case.
 *
 * BUILT ONLY AFTER THE RUN STOOD THERE, as the unit's brief makes its condition.  With
 * stand-in 5 alone the run stopped (docs/handoff/gate3-run-8.md, section 2) at
 *     FATAL unimplemented import originsdk.dll!OriginGetDefaultUser
 *       called from engine.dll+0x1849df   thread 0x1004 ""
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY.  OriginSDK.dll was not
 * read, disassembled, lifted or run.  The engine's code (tools/scout/flow_show.py engine.dll
 * 0x184890 --all --from 0x1849c8 --to 0x1849f8) sets up no argument, makes the call at
 * +0x1849da (five bytes: the next instruction is at +0x1849df) and tests the whole of `rax`.
 * The import has that one call site in the game's DLLs (flow_refs.py engine.dll imp
 * '^OriginGetDefaultUser$': 1 site).
 *
 * THE STOP.  The post names the call: "the call right behind it at engine+0x1849da, with no
 * argument".  There is no argument to compare, so the stand-in compares where the call came
 * from: the return address on the guest stack must be engine.dll's base + 0x1849df.  A call
 * that arrives any other way is not the posted call and halts.  A call while engine.dll is
 * not loaded stops too.
 *
 * CALLED EVERY FRAME: the first answer is logged, the answers after it are only counted
 * (rt_standin6_answers); `gate3 run` prints the count at every end of a run.
 *
 * THE PLACEHOLDER USER (11 October 2026, later the same day).  The owner's words: "go for the
 * 2nd option, then we can try the real one if that doesn't work I do own the game, so proving
 * that to the game is ok, but try the made up one first"; the answers were posted before this
 * was written (issue 32, comment 6114036238): "OriginGetDefaultUser: the fixed number 1
 * instead of 0.  It is a made-up id, not the owner's and not taken from anywhere".  When the
 * program has set rt_standin_user to RT_STANDIN_USER_PLACEHOLDER (rt/standin.h; `gate3 run`
 * does for GATE3_USER=placeholder) the answer to the same call, from the same call site and
 * with the same stop for any other, is RESULT 1 in the whole 64-bit register.  The engine
 * then stores it as its user id and asks for the profile in the same frame
 * (engine+0x183c40: OriginGetProfile at +0x183c91), which is a halting stub: nothing else of
 * that post is built (docs/handoff/gate3-run-11.md says why).  NOBODY stays the answer of
 * every program that does not select the placeholder.
 *
 * It reads the return address and writes nothing to guest memory.  The tests' two variables
 * (rt_standin56_test_module, rt_standin56_test_return_rva; NULL and 0 in every program) are
 * those of stand-in 5 (OriginUpdate.c, rt/standin.h). */
#include <stdatomic.h>

#include "rt/shim.h"
#include "rt/standin.h"

RtStandinUser rt_standin_user;                      /* RT_STANDIN_USER_NOBODY: rt/standin.h, THE USER */

static _Atomic unsigned long long answers;

unsigned long long rt_standin6_answers(void) { return atomic_load(&answers); }

SHIM(originsdk, ULONGLONG, OriginGetDefaultUser)
{
    const char *const name = rt_standin56_test_module ? rt_standin56_test_module : RT_STANDIN_56_CALLER_MODULE;
    const uint32_t rva = rt_standin56_test_module ? rt_standin56_test_return_rva : RT_STANDIN_6_RETURN_RVA;
    const RtModule *engine = rt_module_find(name);
    const uint64_t ret = LD64(ctx->r[4]);               /* the guest's return address: rsp points at it on entry */
    if (engine == NULL || !engine->loaded || ret != engine->base + rva)
        RT_UNIMPLEMENTED(ctx, "stand-in 6 was called from somewhere other than the one call it answers (the engine's per-frame call at "
                         RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to +%#x): the return address is %#llx (%s is %s)",
                         (unsigned)RT_STANDIN_6_CALL_RVA, (unsigned)RT_STANDIN_6_RETURN_RVA, (unsigned long long)ret,
                         name, engine && engine->loaded ? "loaded" : "not loaded");
    if (rt_standin_user == RT_STANDIN_USER_PLACEHOLDER) {
        if (atomic_fetch_add(&answers, 1) == 0)
            rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 6 answered: originsdk.dll!OriginGetDefaultUser() from " RT_STANDIN_56_CALLER_MODULE
                   "+%#x: result %llu, THE PLACEHOLDER USER: a made-up id, not the owner's and taken from nowhere (issue 32, comment "
                   "6114036238), nothing written and nothing called back; this is its first answer: the answers after this one are "
                   "counted and not logged; EA's SDK is not here and did not answer", (unsigned)RT_STANDIN_6_CALL_RVA,
                   (unsigned long long)RT_STANDIN_PLACEHOLDER_USER_ID);
        return RT_STANDIN_PLACEHOLDER_USER_ID;
    }
    if (atomic_fetch_add(&answers, 1) == 0)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 6 answered: originsdk.dll!OriginGetDefaultUser() from " RT_STANDIN_56_CALLER_MODULE
               "+%#x: result 0, \"nobody is signed in\": no user is named and no id is supplied, nothing written and nothing called "
               "back; this is its first answer: the call comes every frame, the answers after this one are counted and not logged; "
               "EA's SDK is not here and did not answer (issue 32)", (unsigned)RT_STANDIN_6_CALL_RVA);
    return 0;
}
