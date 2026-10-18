/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's request (the placeholder's id 1, the text TITANFALL2-PC-SERVER, the callback engine+0x1857b0, 0, 30000, 0, from engine+0x18437b) with the placeholder user selected: result 0, accepted; it never completes: nothing is written, the callback's address is not kept and nothing is ever called back, no code exists; the first answer is logged, every answer counted
 * RT-TRAPS:  stand-in 8 was called in a shape other than the one it answers
 */

/* STAND-IN 8 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * A row of rt_standin_table (rt/standin.h) that `gate3 run` hands over for
 * GATE3_USER=placeholder only, unless GATE3_NO_STANDIN_AUTH_CODE=1 (gate3/run.c); everywhere
 * else the import is the stub that halts the run.
 *
 * What it is: the answer posted on issue 32 on 11 October 2026 before this file was written
 * (RT-DOC above, row 8: "result 0 (accepted).  It never completes: the callback is never
 * called and no code exists") to the engine's call
 *
 *     originsdk.dll!OriginRequestAuthCode(1, "TITANFALL2-PC-SERVER", engine+0x1857b0, 0, 30000, 0)
 *
 * made at engine+0x18437b (in engine+0x1842e0) on the main thread.  THERE IS NO AUTHORISATION
 * CODE: none is made up, none is handed over, the engine's callback is never called and its
 * address is not kept.  The request is counted and forgotten.
 *
 * BUILT ONLY AFTER THE RUN STOOD THERE (docs/handoff/gate3-run-11.md, section 3c):
 *     FATAL unimplemented import originsdk.dll!OriginRequestAuthCode
 *       called from engine.dll+0x184380   thread 0x1004 ""
 *       rcx=1 rdx=<engine.dll+0x602850, the text> r8=<engine.dll+0x1857b0> r9=0  arg5 0x7530  arg6 0
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY (tools/scout/flow_show.py
 * engine.dll 0x1842e0 --all; flow_cstr.py engine.dll 0x602850).  OriginSDK.dll was not read,
 * disassembled, lifted or run.  The engine sets the fifth argument as 32 bits (the upper half
 * of its slot is not set), the others as whole registers or slots.  It sets its byte
 * engine+0x13978257 before the call and does not ask again while that byte is set; a result
 * that is not 0 would be stored as the kit's error.
 *
 * THE STOP.  Any other shape halts and nothing is counted: another call site, another user
 * id, another text, another callback, a fourth or sixth argument that is not 0, a time other
 * than 30000, the user "nobody". */
#include <stdatomic.h>
#include <string.h>

#include "rt/shim.h"
#include "rt/standin.h"

static _Atomic unsigned long long answers;

unsigned long long rt_standin8_answers(void) { return atomic_load(&answers); }

SHIM(originsdk, DWORD, OriginRequestAuthCode, ULONG_PTR user, char *client, ULONG_PTR callback, ULONG_PTR fourth, DWORD timeout, ULONG_PTR sixth)
{
    uint64_t ret;
    const RtModule *engine;
    const bool site = rt_standin_site(ctx, RT_STANDIN_8_RETURN_RVA, &ret, &engine);
    const uint32_t cb = rt_standin56_test_module ? rt_standin7_test_callback_rva : RT_STANDIN_8_CALLBACK_RVA;
    if (rt_standin_user != RT_STANDIN_USER_PLACEHOLDER || !site || user != RT_STANDIN_PLACEHOLDER_USER_ID || client == NULL
        || strcmp(client, RT_STANDIN_8_CLIENT_TEXT) != 0 || callback != engine->base + cb || fourth != 0
        || timeout != RT_STANDIN_8_TIMEOUT_MS || sixth != 0)
        RT_UNIMPLEMENTED(ctx, "stand-in 8 was called in a shape other than the one it answers (the placeholder user's request "
                         "OriginRequestAuthCode(%llu, \"" RT_STANDIN_8_CLIENT_TEXT "\", " RT_STANDIN_56_CALLER_MODULE "+%#x, 0, %u, 0) from "
                         RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to +%#x): the user is %s, the return address is %#llx, the "
                         "arguments are %#llx, %#llx, %#llx, %#llx, %#x, %#llx",
                         (unsigned long long)RT_STANDIN_PLACEHOLDER_USER_ID, (unsigned)RT_STANDIN_8_CALLBACK_RVA,
                         (unsigned)RT_STANDIN_8_TIMEOUT_MS, (unsigned)RT_STANDIN_8_CALL_RVA, (unsigned)RT_STANDIN_8_RETURN_RVA,
                         rt_standin_user == RT_STANDIN_USER_PLACEHOLDER ? "the placeholder" : "nobody", (unsigned long long)ret,
                         (unsigned long long)user, (unsigned long long)(uintptr_t)client, (unsigned long long)callback,
                         (unsigned long long)fourth, (unsigned)timeout, (unsigned long long)sixth);
    if (atomic_fetch_add(&answers, 1) == 0)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 8 answered: originsdk.dll!OriginRequestAuthCode(%llu, \"" RT_STANDIN_8_CLIENT_TEXT "\", "
               RT_STANDIN_56_CALLER_MODULE "+%#x, 0, %u, 0) from " RT_STANDIN_56_CALLER_MODULE "+%#x: result 0, accepted, AND IT NEVER "
               "COMPLETES: no authorisation code exists, the callback's address is not kept and nothing is ever called back; this is "
               "its first answer: the answers after this one are counted and not logged; EA's SDK is not here and did not answer "
               "(issue 32, comment 6114036238)", (unsigned long long)user, (unsigned)RT_STANDIN_8_CALLBACK_RVA,
               (unsigned)RT_STANDIN_8_TIMEOUT_MS, (unsigned)RT_STANDIN_8_CALL_RVA);
    return 0;
}
