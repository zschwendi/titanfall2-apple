/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's query (the placeholder's id 1, six zeros, the text Titanfall2PC, the callback engine+0x181f60, 0, 10000, 0, from engine+0x183fe2) with the placeholder user selected: result 0, accepted; it never completes: nothing is written, the callback's address is not kept and nothing is ever called back, no list and no entitlement exists; the first answer is logged, every answer counted
 * RT-TRAPS:  stand-in 9 was called in a shape other than the one it answers
 */

/* STAND-IN 9 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * A row of rt_standin_table (rt/standin.h) that `gate3 run` hands over for
 * GATE3_USER=placeholder only, unless GATE3_NO_STANDIN_ENTITLEMENTS=1 (gate3/run.c); everywhere
 * else the import is the stub that halts the run.
 *
 * What it is: the answer posted on issue 32 on 11 October 2026 before this file was written
 * (RT-DOC above, row 9: "result 0 (accepted).  It never completes: no list, no entitlement
 * claimed") to the engine's call
 *
 *     originsdk.dll!OriginQueryEntitlements(1, 0, 0, 0, 0, 0, 0, "Titanfall2PC", engine+0x181f60, 0, 10000, 0)
 *
 * made at engine+0x183fe2 (in engine+0x183f40) on the main thread.  NO ENTITLEMENT IS CLAIMED:
 * no list is made up, the engine's callback is never called and its address is not kept.
 * The query is counted and forgotten.
 *
 * BUILT ONLY AFTER THE RUN STOOD THERE (docs/handoff/gate3-run-11.md, section 3c):
 *     FATAL unimplemented import originsdk.dll!OriginQueryEntitlements
 *       called from engine.dll+0x183fe7   thread 0x1004 ""
 *       rcx=1 rdx=0 r8=0 r9=0  arg5 to arg7 0  arg8 <the text>  arg9 <engine.dll+0x181f60>  arg10 0  arg11 ...00002710  arg12 0
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY (tools/scout/flow_show.py
 * engine.dll 0x183f40 --all).  OriginSDK.dll was not read, disassembled, lifted or run.  The
 * engine sets the eleventh argument as 32 bits (the upper half of its slot is not set), the
 * others as whole registers or slots.  A result that is not 0 would be stored as the kit's
 * error.
 *
 * THE STOP.  Any other shape halts and nothing is counted: another call site, another user
 * id, any of the seven zeros not 0, another text, another callback, a time other than 10000,
 * the user "nobody". */
#include <stdatomic.h>
#include <string.h>

#include "rt/shim.h"
#include "rt/standin.h"

static _Atomic unsigned long long answers;

unsigned long long rt_standin9_answers(void) { return atomic_load(&answers); }

SHIM(originsdk, DWORD, OriginQueryEntitlements, ULONG_PTR user, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5, ULONG_PTR a6, ULONG_PTR a7, char *game, ULONG_PTR callback, ULONG_PTR a10, DWORD timeout, ULONG_PTR a12)
{
    uint64_t ret;
    const RtModule *engine;
    const bool site = rt_standin_site(ctx, RT_STANDIN_9_RETURN_RVA, &ret, &engine);
    const uint32_t cb = rt_standin56_test_module ? rt_standin7_test_callback_rva : RT_STANDIN_9_CALLBACK_RVA;
    if (rt_standin_user != RT_STANDIN_USER_PLACEHOLDER || !site || user != RT_STANDIN_PLACEHOLDER_USER_ID
        || (a2 | a3 | a4 | a5 | a6 | a7 | a10 | a12) != 0 || game == NULL || strcmp(game, RT_STANDIN_9_GAME_TEXT) != 0
        || callback != engine->base + cb || timeout != RT_STANDIN_9_TIMEOUT_MS)
        RT_UNIMPLEMENTED(ctx, "stand-in 9 was called in a shape other than the one it answers (the placeholder user's query "
                         "OriginQueryEntitlements(%llu, six zeros, \"" RT_STANDIN_9_GAME_TEXT "\", " RT_STANDIN_56_CALLER_MODULE "+%#x, 0, "
                         "%u, 0) from " RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to +%#x): the user is %s, the return address is "
                         "%#llx, the arguments are %#llx, %#llx, %#llx, %#llx, %#llx, %#llx, %#llx, %#llx, %#llx, %#llx, %#x, %#llx",
                         (unsigned long long)RT_STANDIN_PLACEHOLDER_USER_ID, (unsigned)RT_STANDIN_9_CALLBACK_RVA,
                         (unsigned)RT_STANDIN_9_TIMEOUT_MS, (unsigned)RT_STANDIN_9_CALL_RVA, (unsigned)RT_STANDIN_9_RETURN_RVA,
                         rt_standin_user == RT_STANDIN_USER_PLACEHOLDER ? "the placeholder" : "nobody", (unsigned long long)ret,
                         (unsigned long long)user, (unsigned long long)a2, (unsigned long long)a3, (unsigned long long)a4,
                         (unsigned long long)a5, (unsigned long long)a6, (unsigned long long)a7, (unsigned long long)(uintptr_t)game,
                         (unsigned long long)callback, (unsigned long long)a10, (unsigned)timeout, (unsigned long long)a12);
    if (atomic_fetch_add(&answers, 1) == 0)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 9 answered: originsdk.dll!OriginQueryEntitlements(%llu, six zeros, \""
               RT_STANDIN_9_GAME_TEXT "\", " RT_STANDIN_56_CALLER_MODULE "+%#x, 0, %u, 0) from " RT_STANDIN_56_CALLER_MODULE "+%#x: result 0, "
               "accepted, AND IT NEVER COMPLETES: no list and no entitlement exists, none is claimed, the callback's address is not kept "
               "and nothing is ever called back; this is its first answer: the answers after this one are counted and not logged; EA's "
               "SDK is not here and did not answer (issue 32, comment 6114036238)", (unsigned long long)user,
               (unsigned)RT_STANDIN_9_CALLBACK_RVA, (unsigned)RT_STANDIN_9_TIMEOUT_MS, (unsigned)RT_STANDIN_9_CALL_RVA);
    return 0;
}
