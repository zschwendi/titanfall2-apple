/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's call (the placeholder's id 1, the number 3 or 6, three pointers to texts of the game's own status or NULL, from engine+0x184bad) with the placeholder user selected: result 0, accepted; nothing is done with the texts: they are not read, nothing is written, nothing is sent anywhere and nothing is called back; the first answer is logged, every answer counted
 * RT-TRAPS:  stand-in 13 was called in a shape other than the one it answers
 */

/* STAND-IN 13 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * A row of rt_standin_table (rt/standin.h) that `gate3 run` hands over for
 * GATE3_USER=placeholder only, unless GATE3_NO_STANDIN_PRESENCE=1 (gate3/run.c); everywhere
 * else the import is the stub that halts the run.
 *
 * What it is: the answer posted on issue 32 on 11 October 2026 before this file was written
 * (RT-DOC above, row 13: "result 0 (accepted); nothing is done with the texts") to the
 * engine's call
 *
 *     originsdk.dll!OriginSetPresence(1, 3 or 6, text or NULL, text or NULL, text or NULL)
 *
 * made at engine+0x184bad, in the engine's per-frame function for EA's kit, at intervals of
 * the engine's own choosing, on the main thread.  In a real start it tells EA's service what
 * the user is doing.  HERE NOTHING IS TOLD TO ANYONE: the texts are not even read, there is no
 * service and no host socket.
 *
 * BUILT ONLY AFTER THE RUN STOOD THERE (docs/handoff/gate3-run-11.md, section 3c, run p7):
 *     FATAL unimplemented import originsdk.dll!OriginSetPresence
 *       called from engine.dll+0x184bb2   thread 0x1004 ""
 *       rcx=1 rdx=3 r8=<engine.dll+0x1397ac46> r9=0  arg5 0
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY (tools/scout/flow_show.py
 * engine.dll 0x184890 --all --from 0x184b25 --to 0x184bd0).  OriginSDK.dll was not read,
 * disassembled, lifted or run.  The engine passes its user id, the number 3 (or 6 when a flag
 * of its own and its third text are set), and for each of three buffers of its own
 * (engine+0x1397ac46, +0x1397ad46, +0x1397ab46) the buffer's address if its text is not empty,
 * else 0.  A result that is not 0 would be stored as the kit's error.
 *
 * THE STOP.  Any other shape halts and nothing is counted: another call site, another user
 * id, a number other than 3 or 6, the user "nobody".  The three pointers are not compared and
 * not followed. */
#include <stdatomic.h>

#include "rt/shim.h"
#include "rt/standin.h"

static _Atomic unsigned long long answers;

unsigned long long rt_standin13_answers(void) { return atomic_load(&answers); }

SHIM(originsdk, DWORD, OriginSetPresence, ULONG_PTR user, ULONG_PTR kind, ULONG_PTR text1, ULONG_PTR text2, ULONG_PTR text3)
{
    uint64_t ret;
    const RtModule *engine;
    const bool site = rt_standin_site(ctx, RT_STANDIN_13_RETURN_RVA, &ret, &engine);
    if (rt_standin_user != RT_STANDIN_USER_PLACEHOLDER || !site || user != RT_STANDIN_PLACEHOLDER_USER_ID || (kind != 3 && kind != 6))
        RT_UNIMPLEMENTED(ctx, "stand-in 13 was called in a shape other than the one it answers (the placeholder user's "
                         "OriginSetPresence(%llu, 3 or 6, three texts or NULL) from " RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to "
                         "+%#x): the user is %s, the return address is %#llx, the arguments are %#llx, %#llx, %#llx, %#llx, %#llx",
                         (unsigned long long)RT_STANDIN_PLACEHOLDER_USER_ID, (unsigned)RT_STANDIN_13_CALL_RVA,
                         (unsigned)RT_STANDIN_13_RETURN_RVA, rt_standin_user == RT_STANDIN_USER_PLACEHOLDER ? "the placeholder" : "nobody",
                         (unsigned long long)ret, (unsigned long long)user, (unsigned long long)kind, (unsigned long long)text1,
                         (unsigned long long)text2, (unsigned long long)text3);
    if (atomic_fetch_add(&answers, 1) == 0)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 13 answered: originsdk.dll!OriginSetPresence(%llu, %llu, %s, %s, %s) from "
               RT_STANDIN_56_CALLER_MODULE "+%#x: result 0, accepted, AND NOTHING IS DONE WITH THE TEXTS: they are not read, nothing is "
               "written, nothing is told to anyone and nothing is called back; this is its first answer: the answers after this one "
               "are counted and not logged; EA's SDK is not here and did not answer (issue 32, comment 6114036238)",
               (unsigned long long)user, (unsigned long long)kind, text1 ? "a text" : "NULL", text2 ? "a text" : "NULL",
               text3 ? "a text" : "NULL", (unsigned)RT_STANDIN_13_CALL_RVA);
    return 0;
}
