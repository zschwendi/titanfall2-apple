/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's query (the placeholder's id 1, the callback engine+0x184000, 0, 10000, a pointer for a handle, from engine+0x183cf7) with the placeholder user selected: result 0, accepted; the handle the engine asks for is written as 0; it never completes: the callback's address is not kept and nothing is ever called back, no list exists; the first answer is logged, every answer counted
 * RT-TRAPS:  stand-in 11 was called in a shape other than the one it answers
 */

/* STAND-IN 11 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * A row of rt_standin_table (rt/standin.h) that `gate3 run` hands over for
 * GATE3_USER=placeholder only, unless GATE3_NO_STANDIN_FRIENDS=1 (gate3/run.c); everywhere else the import is
 * the stub that halts the run.
 *
 * What it is: the answer posted on issue 32 on 11 October 2026 before this file was written
 * (RT-DOC above, rows 10 and 11: "result 0 (accepted), never completed; a handle the engine
 * asks for is written as 0") to the engine's call
 *
 *     originsdk.dll!OriginQueryFriends(1, engine+0x184000, 0, 10000, <a pointer for a handle>)
 *
 * made at engine+0x183cf7 (in engine+0x183c40, right behind the accepted profile request) on
 * the main thread.  THERE IS NO LIST OF friends: none is made up, the engine's callback is
 * never called and its address is not kept.  The query is counted and forgotten.
 *
 * BUILT ONLY AFTER THE RUN STOOD THERE (docs/handoff/gate3-run-11.md, section 3c, run p6):
 *     FATAL unimplemented import originsdk.dll!OriginQueryFriends
 *       called from engine.dll+0x183cfc   thread 0x1004 ""
 *       rcx=1 rdx=<engine.dll+0x184000> r8=0 r9=0x2710  arg5 <a slot of the engine's stack>
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY (tools/scout/flow_show.py
 * engine.dll 0x183c40 --all).  OriginSDK.dll was not read, disassembled, lifted or run.  The
 * engine sets all five arguments as whole registers or slots, does not test the result and
 * never reads the handle's slot again (a scratch slot of its own frame, 8 bytes; the next 8
 * are its own scratch too).  THE HANDLE'S WIDTH is not knowable from the engine's side: 8
 * bytes of 0 are written, the width of a pointer.
 *
 * THE STOP.  Any other shape halts, nothing is written and nothing counted: another call
 * site, another user id, another callback, a third argument that is not 0, a time other than
 * 10000, a NULL pointer for the handle, the user "nobody". */
#include <stdatomic.h>
#include <string.h>

#include "rt/shim.h"
#include "rt/standin.h"

static _Atomic unsigned long long answers;

unsigned long long rt_standin11_answers(void) { return atomic_load(&answers); }

SHIM(originsdk, DWORD, OriginQueryFriends, ULONG_PTR user, ULONG_PTR callback, ULONG_PTR third, ULONG_PTR timeout, ULONG64 *handle)
{
    uint64_t ret;
    const RtModule *engine;
    const bool site = rt_standin_site(ctx, RT_STANDIN_11_RETURN_RVA, &ret, &engine);
    const uint32_t cb = rt_standin56_test_module ? rt_standin7_test_callback_rva : RT_STANDIN_11_CALLBACK_RVA;
    if (rt_standin_user != RT_STANDIN_USER_PLACEHOLDER || !site || user != RT_STANDIN_PLACEHOLDER_USER_ID
        || callback != engine->base + cb || third != 0 || timeout != RT_STANDIN_11_TIMEOUT_MS || handle == NULL)
        RT_UNIMPLEMENTED(ctx, "stand-in 11 was called in a shape other than the one it answers (the placeholder user's query "
                         "OriginQueryFriends(%llu, " RT_STANDIN_56_CALLER_MODULE "+%#x, 0, %u, a pointer for a handle) from "
                         RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to +%#x): the user is %s, the return address is %#llx, the "
                         "arguments are %#llx, %#llx, %#llx, %#llx, %#llx",
                         (unsigned long long)RT_STANDIN_PLACEHOLDER_USER_ID, (unsigned)RT_STANDIN_11_CALLBACK_RVA,
                         (unsigned)RT_STANDIN_11_TIMEOUT_MS, (unsigned)RT_STANDIN_11_CALL_RVA, (unsigned)RT_STANDIN_11_RETURN_RVA,
                         rt_standin_user == RT_STANDIN_USER_PLACEHOLDER ? "the placeholder" : "nobody", (unsigned long long)ret,
                         (unsigned long long)user, (unsigned long long)callback, (unsigned long long)third, (unsigned long long)timeout,
                         (unsigned long long)(uintptr_t)handle);
    *handle = 0;
    if (atomic_fetch_add(&answers, 1) == 0)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 11 answered: originsdk.dll!OriginQueryFriends(%llu, " RT_STANDIN_56_CALLER_MODULE "+%#x, 0, %u, "
               "%#llx) from " RT_STANDIN_56_CALLER_MODULE "+%#x: result 0, accepted, the handle written as 0 (8 bytes), AND IT NEVER "
               "COMPLETES: no list of friends exists, the callback's address is not kept and nothing is ever called back; this is its "
               "first answer: the answers after this one are counted and not logged; EA's SDK is not here and did not answer (issue "
               "32, comment 6114036238)", (unsigned long long)user, (unsigned)RT_STANDIN_11_CALLBACK_RVA,
               (unsigned)RT_STANDIN_11_TIMEOUT_MS, (unsigned long long)(uintptr_t)handle, (unsigned)RT_STANDIN_11_CALL_RVA);
    return 0;
}
