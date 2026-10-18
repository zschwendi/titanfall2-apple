/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's call (no argument, from engine+0x18483a) with the placeholder user selected: result 1, the made-up persona, in the whole 64-bit register; nothing is written and nothing is called back; every answer is counted; one line is logged for the first answer and none for the answers after it
 * RT-TRAPS:  a call from anywhere but engine+0x18483a (the return address is not engine.dll+0x18483f); a call while engine.dll is not loaded; the user "nobody" (no answer is given for it: the run stops, as at any import without a stand-in)
 */

/* STAND-IN 14 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * It is not in the runtime's shim table (rt_shim_table): it is a row of rt_standin_table
 * (rt/standin.h), and a program has it only if it hands that row to rt_init itself.  `gate3
 * run` does, for GATE3_USER=placeholder only, unless GATE3_NO_STANDIN_PERSONA=1 (gate3/run.c).
 * In every other program, and for every import of originsdk.dll that has no stand-in, the
 * import is the stub that halts the run.
 *
 * What it is: the answer posted on issue 32 on 12 October 2026 (RT-DOC above; the owner's
 * decision of that day: "Placeholder (Recommended)") to the engine's call
 *
 *     originsdk.dll!OriginGetDefaultPersona()
 *
 * made at engine+0x18483a, in the engine's function at engine+0x184820 for EA's kit.  The
 * answer is the placeholder's persona: the fixed number 1, the same made-up number as its user
 * id (RT_STANDIN_PLACEHOLDER_USER_ID).  It is not the owner's persona and is taken from nowhere.
 * Nothing is written to guest memory and nothing is called back.
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY.  OriginSDK.dll was not
 * read, disassembled, lifted or run.  The engine then passes this number as the second argument
 * of OriginGrantAchievement (stand-in 15, OriginGrantAchievement.c).
 *
 * THE STOP.  A call from any other place, while engine.dll is not loaded, or with any user
 * other than the placeholder's, halts the run and nothing is counted. */
#include <stdatomic.h>

#include "rt/shim.h"
#include "rt/standin.h"

static _Atomic unsigned long long answers;

unsigned long long rt_standin14_answers(void) { return atomic_load(&answers); }

SHIM(originsdk, ULONGLONG, OriginGetDefaultPersona)
{
    uint64_t ret;
    const RtModule *engine;
    const bool site = rt_standin_site(ctx, RT_STANDIN_14_RETURN_RVA, &ret, &engine);
    if (rt_standin_user != RT_STANDIN_USER_PLACEHOLDER || !site)
        RT_UNIMPLEMENTED(ctx, "stand-in 14 was called in a shape other than the one it answers (the placeholder user's "
                         "OriginGetDefaultPersona() from " RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to +%#x): the user is %s, "
                         "the return address is %#llx (%s is %s)",
                         (unsigned)RT_STANDIN_14_CALL_RVA, (unsigned)RT_STANDIN_14_RETURN_RVA,
                         rt_standin_user == RT_STANDIN_USER_PLACEHOLDER ? "the placeholder" : "nobody", (unsigned long long)ret,
                         rt_standin56_test_module ? rt_standin56_test_module : RT_STANDIN_56_CALLER_MODULE,
                         engine && engine->loaded ? "loaded" : "not loaded");
    if (atomic_fetch_add(&answers, 1) == 0)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 14 answered: originsdk.dll!OriginGetDefaultPersona() from " RT_STANDIN_56_CALLER_MODULE
               "+%#x: result %llu, THE PLACEHOLDER PERSONA: a made-up number, the same as its user id, not the owner's and taken from "
               "nowhere (issue 32, comment 6125043112), nothing written and nothing called back; this is its first answer: the answers "
               "after this one are counted and not logged; EA's SDK is not here and did not answer",
               (unsigned)RT_STANDIN_14_CALL_RVA, (unsigned long long)RT_STANDIN_PLACEHOLDER_USER_ID);
    return RT_STANDIN_PLACEHOLDER_USER_ID;
}
