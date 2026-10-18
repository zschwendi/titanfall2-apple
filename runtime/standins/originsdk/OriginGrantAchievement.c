/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's call OriginGrantAchievement(1, 1, n, 1, "{331F357B-49AB-45DC-852F-4F047EADDCA3}", 10000, engine+0x182c60, 0) from engine+0x18487b with the placeholder user selected: result 0, accepted; nothing is done with it: no achievement is recorded, nothing is written and the callback is neither kept nor called; any number n; every call is logged with its number and counted
 * RT-TRAPS:  a call from anywhere but engine+0x18487b (the return address is not engine.dll+0x184880); a call while engine.dll is not loaded; a user id that is not 1, a persona that is not 1, a fourth argument that is not 1, a text that is not the posted one (or NULL), a sixth argument that is not 10000, a callback that is not engine.dll+0x182c60, an eighth argument that is not 0; the user "nobody"
 */

/* STAND-IN 15 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * It is not in the runtime's shim table (rt_shim_table): it is a row of rt_standin_table
 * (rt/standin.h), and a program has it only if it hands that row to rt_init itself.  `gate3
 * run` does, for GATE3_USER=placeholder only, unless GATE3_NO_STANDIN_ACHIEVEMENT=1
 * (gate3/run.c).  In every other program, and for every import of originsdk.dll that has no
 * stand-in, the import is the stub that halts the run.
 *
 * What it is: the answer posted on issue 32 on 12 October 2026 (RT-DOC above; the owner's
 * decision of that day: "Placeholder (Recommended)") to the engine's call
 *
 *     originsdk.dll!OriginGrantAchievement(1, 1, n, 1, "{331F357B-49AB-45DC-852F-4F047EADDCA3}", 10000, engine+0x182c60, 0)
 *
 * made at engine+0x18487b, in the engine's function at engine+0x184820 for EA's kit, with the
 * persona that stand-in 14 gave (1).  The result 0 means "accepted".  NOTHING IS DONE WITH IT:
 * no achievement is recorded anywhere, nothing is written to guest memory, the callback's
 * address is not kept and it is never called.  The engine does not test the result.  Every
 * call is logged with its number and counted; n may be any number.
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY.  OriginSDK.dll was not
 * read, disassembled, lifted or run.
 *
 * THE STOP.  Any other shape halts and nothing is counted: another call site, another user,
 * persona, fourth argument, text or sixth argument, another callback or eighth argument, the
 * user "nobody". */
#include <stdatomic.h>
#include <string.h>

#include "rt/shim.h"
#include "rt/standin.h"

static _Atomic unsigned long long answers;

unsigned long long rt_standin15_answers(void) { return atomic_load(&answers); }

SHIM(originsdk, DWORD, OriginGrantAchievement, ULONG_PTR user, ULONG_PTR persona, DWORD number, DWORD fourth, char *text, DWORD timeout, ULONG_PTR callback, ULONG_PTR eighth)
{
    uint64_t ret;
    const RtModule *engine;
    const bool site = rt_standin_site(ctx, RT_STANDIN_15_RETURN_RVA, &ret, &engine);
    const uint32_t cb = rt_standin56_test_module ? rt_standin7_test_callback_rva : RT_STANDIN_15_CALLBACK_RVA;
    if (rt_standin_user != RT_STANDIN_USER_PLACEHOLDER || !site || user != RT_STANDIN_PLACEHOLDER_USER_ID
        || persona != RT_STANDIN_PLACEHOLDER_USER_ID || fourth != 1 || text == NULL
        || strcmp(text, RT_STANDIN_15_TEXT) != 0 || timeout != 10000 || callback != engine->base + cb || eighth != 0)
        RT_UNIMPLEMENTED(ctx, "stand-in 15 was called in a shape other than the one it answers (the placeholder user's "
                         "OriginGrantAchievement(%llu, %llu, a number, 1, \"" RT_STANDIN_15_TEXT "\", 10000, " RT_STANDIN_56_CALLER_MODULE
                         "+%#x, 0) from " RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to +%#x): the user is %s, the return address "
                         "is %#llx, the arguments are %#llx, %#llx, %u, %u, %#llx, %u, %#llx, %#llx",
                         (unsigned long long)RT_STANDIN_PLACEHOLDER_USER_ID, (unsigned long long)RT_STANDIN_PLACEHOLDER_USER_ID,
                         (unsigned)RT_STANDIN_15_CALLBACK_RVA, (unsigned)RT_STANDIN_15_CALL_RVA, (unsigned)RT_STANDIN_15_RETURN_RVA,
                         rt_standin_user == RT_STANDIN_USER_PLACEHOLDER ? "the placeholder" : "nobody", (unsigned long long)ret,
                         (unsigned long long)user, (unsigned long long)persona, (unsigned)number, (unsigned)fourth,
                         (unsigned long long)(uintptr_t)text, (unsigned)timeout, (unsigned long long)callback, (unsigned long long)eighth);
    const unsigned long long n = atomic_fetch_add(&answers, 1) + 1;
    rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 15 answered: originsdk.dll!OriginGrantAchievement(%llu, %llu, %u, 1, \"" RT_STANDIN_15_TEXT
           "\", 10000, " RT_STANDIN_56_CALLER_MODULE "+%#x, 0) from " RT_STANDIN_56_CALLER_MODULE "+%#x: result 0, accepted, AND NOTHING IS "
           "DONE WITH IT: no achievement is recorded, nothing is written, the callback is not kept and never called (call number %llu; "
           "issue 32, comment 6125043112)", (unsigned long long)user, (unsigned long long)persona, (unsigned)number,
           (unsigned)RT_STANDIN_15_CALLBACK_RVA, (unsigned)RT_STANDIN_15_CALL_RVA, n);
    return 0;
}
