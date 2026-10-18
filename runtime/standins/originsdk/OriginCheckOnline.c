/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's call (a pointer to a byte the engine has set to 0, from engine+0x183a39) with the placeholder user selected: result 0 and the byte left 0, "not online"; nothing is written and nothing is called back; the first answer is logged, every answer counted
 * RT-TRAPS:  stand-in 12 was called in a shape other than the one it answers
 */

/* STAND-IN 12 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * A row of rt_standin_table (rt/standin.h) that `gate3 run` hands over for
 * GATE3_USER=placeholder only, unless GATE3_NO_STANDIN_CHECK_ONLINE=1 (gate3/run.c); everywhere
 * else the import is the stub that halts the run.
 *
 * What it is: the answer posted on issue 32 on 11 October 2026 before this file was written
 * (RT-DOC above, row 12: "result 0, the engine's byte left 0: 'not online'") to the engine's
 * call
 *
 *     originsdk.dll!OriginCheckOnline(<a pointer to a byte the engine has just set to 0>)
 *
 * made at engine+0x183a39 (in engine+0x183a00, from the per-frame function for EA's kit) on
 * the main thread.  THE PLACEHOLDER USER IS NOT ONLINE, which is true here: there is no
 * connection to EA and no host socket.  The byte is not written.
 *
 * BUILT ONLY AFTER THE RUN STOOD THERE (docs/handoff/gate3-run-11.md, section 3c, run p8):
 *     FATAL unimplemented import originsdk.dll!OriginCheckOnline
 *       called from engine.dll+0x183a3e   thread 0x1004 ""
 *       rcx=<a slot of the engine's stack>   (rdx, r8, r9: leftovers; the engine sets none)
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY (tools/scout/flow_show.py
 * engine.dll 0x183a00 --all).  OriginSDK.dll was not read, disassembled, lifted or run.  The
 * engine sets one argument, does not test the result, and takes "the byte is not 0" as
 * "online", which it stores (engine+0x13978251) and answers from until its own byte
 * engine+0x13978252 asks for a new check.
 *
 * THE STOP.  Any other shape halts and nothing is counted: another call site, a NULL
 * pointer, a byte that is not 0 on entry, the user "nobody". */
#include <stdatomic.h>

#include "rt/shim.h"
#include "rt/standin.h"

static _Atomic unsigned long long answers;

unsigned long long rt_standin12_answers(void) { return atomic_load(&answers); }

SHIM(originsdk, DWORD, OriginCheckOnline, BYTE *online)
{
    uint64_t ret;
    const RtModule *engine;
    const bool site = rt_standin_site(ctx, RT_STANDIN_12_RETURN_RVA, &ret, &engine);
    if (rt_standin_user != RT_STANDIN_USER_PLACEHOLDER || !site || online == NULL || *online != 0)
        RT_UNIMPLEMENTED(ctx, "stand-in 12 was called in a shape other than the one it answers (the placeholder user's "
                         "OriginCheckOnline(a pointer to a byte that is 0) from " RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to "
                         "+%#x): the user is %s, the return address is %#llx, the pointer is %#llx, the byte is %d",
                         (unsigned)RT_STANDIN_12_CALL_RVA, (unsigned)RT_STANDIN_12_RETURN_RVA,
                         rt_standin_user == RT_STANDIN_USER_PLACEHOLDER ? "the placeholder" : "nobody", (unsigned long long)ret,
                         (unsigned long long)(uintptr_t)online, online ? (int)*online : -1);
    if (atomic_fetch_add(&answers, 1) == 0)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 12 answered: originsdk.dll!OriginCheckOnline(%#llx) from " RT_STANDIN_56_CALLER_MODULE
               "+%#x: result 0 and the engine's byte left 0, \"NOT ONLINE\" (true here: there is no connection to EA); nothing written "
               "and nothing called back; this is its first answer: the answers after this one are counted and not logged; EA's SDK is "
               "not here and did not answer (issue 32, comment 6114036238)", (unsigned long long)(uintptr_t)online,
               (unsigned)RT_STANDIN_12_CALL_RVA);
    return 0;
}
