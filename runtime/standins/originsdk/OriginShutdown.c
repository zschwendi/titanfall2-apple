/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's call (no argument, from engine+0x18471b, returning to +0x184720) for both users: result 0, "no error"; nothing is written, nothing is called back, nothing is sent anywhere; every answer is counted; one line is logged for the first answer and none for the answers after it
 * RT-TRAPS:  a call from anywhere but engine+0x18471b (the return address is not engine.dll+0x184720); a call while engine.dll is not loaded
 */

/* STAND-IN 16 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * It is not in the runtime's shim table (rt_shim_table): it is a row of rt_standin_table
 * (rt/standin.h), and a program has it only if it hands that row to rt_init itself.  `gate3
 * run` does, for both GATE3_USER=placeholder and GATE3_USER=nobody, unless
 * GATE3_NO_STANDIN_SHUTDOWN=1 (gate3/run.c).  In every other program, and for every import of
 * originsdk.dll that has no stand-in, the import is the stub that halts the run.
 *
 * What it is: the answer posted on issue 32 on 13 October 2026 (RT-DOC above; the owner's
 * decision of that day: "yes to the stand-in") to the engine's call
 *
 *     originsdk.dll!OriginShutdown()
 *
 * made at engine+0x18471b, in the engine's unload at engine+0x1846b0 for EA's kit, when the
 * engine's "initialised" byte is set.  The call has no argument and carries no id and no data
 * of anyone, so it answers for BOTH users.  The answer is result 0, "no error".  The engine
 * then overwrites the result register without reading it: the engine ignores the answer.
 * NOTHING IS DONE: nothing is written to guest memory, nothing is called back, nothing is sent
 * anywhere.  Every call is logged (the first) and counted.
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY.  OriginSDK.dll was not
 * read, disassembled, lifted or run.
 *
 * THE STOP.  A call from any other place, or while engine.dll is not loaded, halts the run and
 * nothing is counted. */
#include <stdatomic.h>

#include "rt/shim.h"
#include "rt/standin.h"

static _Atomic unsigned long long answers;

unsigned long long rt_standin16_answers(void) { return atomic_load(&answers); }

SHIM(originsdk, DWORD, OriginShutdown)
{
    uint64_t ret;
    const RtModule *engine;
    const bool site = rt_standin_site(ctx, RT_STANDIN_16_RETURN_RVA, &ret, &engine);
    if (!site)
        RT_UNIMPLEMENTED(ctx, "stand-in 16 was called in a shape other than the one it answers (OriginShutdown() from "
                         RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to +%#x): the return address is %#llx (%s is %s)",
                         (unsigned)RT_STANDIN_16_CALL_RVA, (unsigned)RT_STANDIN_16_RETURN_RVA, (unsigned long long)ret,
                         rt_standin56_test_module ? rt_standin56_test_module : RT_STANDIN_56_CALLER_MODULE,
                         engine && engine->loaded ? "loaded" : "not loaded");
    if (atomic_fetch_add(&answers, 1) == 0)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 16 answered: originsdk.dll!OriginShutdown() from " RT_STANDIN_56_CALLER_MODULE
               "+%#x: result 0, \"no error\" (issue 32, comment 6160020362), NOTHING DONE: nothing written, nothing called back, "
               "nothing sent; this is its first answer: the answers after this one are counted and not logged; EA's SDK is not here "
               "and did not answer",
               (unsigned)RT_STANDIN_16_CALL_RVA);
    return 0;
}
