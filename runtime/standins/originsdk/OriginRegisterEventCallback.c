/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the observed call (an event number, the engine's callback engine+0x182130, third argument 0): result 0, the registration counted, nothing stored and nothing ever called back; each of the nine event numbers the engine registers is answered the same; one line is logged for each answer
 * RT-TRAPS:  any other callback address (NULL, another function of the engine, an address of another module); a third argument that is not 0; a call while engine.dll is not loaded
 */

/* STAND-IN 4 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * It is not in the runtime's shim table (rt_shim_table): it is a row of rt_standin_table
 * (rt/standin.h), and a program has it only if it hands that row to rt_init itself.  `gate3
 * run` does, unless GATE3_NO_STANDIN_EVENT_CALLBACKS=1 (gate3/run.c).  In every other
 * program, and for every import of originsdk.dll that has no stand-in, the import is the stub
 * that halts the run.
 *
 * What it is: the one answer, posted on issue 32 on 11 October 2026 before this file was
 * written (RT-DOC above; the owner's rule is the plan's answer 2.1), to the engine's nine calls
 *
 *     originsdk.dll!OriginRegisterEventCallback(<event number>, engine+0x182130, 0)
 *
 * made at engine+0x18386f to +0x183905 on the main thread, right after the call stand-in 3
 * answers (docs/handoff/gate3-run-6.md, section 4, point 5).  The answer is: result 0; the
 * registration is counted; NOTHING IS EVER CALLED BACK, because no event of EA's SDK happens
 * in this runtime.  This function does not even keep the callback's address: it compares it
 * and forgets it, so nothing here could call it.  The answer gives the engine nothing: no
 * identity, no user or persona id, no entitlement, no authorisation code, nothing of an
 * account, no setting.  The engine tests no result of the nine calls.
 *
 * BUILT ONLY AFTER THE RUN SHOWED THE SHAPE the post makes its condition.  With stand-in 3
 * alone the run stopped at the first registration (docs/handoff/gate3-run-7.md, section 3):
 *     FATAL unimplemented import originsdk.dll!OriginRegisterEventCallback
 *       called from engine.dll+0x183874
 *       rcx=0000000000000001 rdx=0000010000a22130 r8=0000000000000000
 * with engine.dll at 0x100008a0000 in that run: rdx is engine+0x182130 and r8 is 0, as the
 * code reading gave them.
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY.  OriginSDK.dll was not
 * read, disassembled, lifted or run.  The engine's code (tools/scout/flow_show.py engine.dll
 * 0x183780 --all) sets, before each of the nine calls, r8 to 0 (`xor r8d, r8d`), rdx to the
 * address of its own function at +0x182130 (`lea rdx, [rip - ...]`) and ecx to the event
 * number (1, 0xb, 2, 9, 0, 0x10, 8, 5, 4, in that order), and reads nothing afterwards.
 *
 * THE STOPS, as posted: any other callback address, and a third argument that is not 0, each
 * compared as a whole 64-bit register (all three instructions define the whole register).
 * The event number is not restricted (the post restricts the callback and the third argument);
 * it is printed in the log line of every answer.  A call while engine.dll is not loaded has no
 * engine+0x182130 to compare with and stops too.
 *
 * It reads its three arguments and writes nothing to guest memory.
 *
 * rt_standin4_test_module (rt/standin.h) is NULL in every program; the stand-in's own test,
 * whose runtime has no engine.dll, names its test module there so that "the module's base +
 * 0x182130" has a value to be compared with. */
#include <stdatomic.h>

#include "rt/shim.h"
#include "rt/standin.h"

const char *rt_standin4_test_module;

static _Atomic unsigned registrations;

unsigned rt_standin4_registrations(void) { return atomic_load(&registrations); }

SHIM(originsdk, DWORD, OriginRegisterEventCallback, ULONG_PTR event, ULONG_PTR callback, ULONG_PTR third)
{
    const RtModule *engine = rt_module_find(rt_standin4_test_module ? rt_standin4_test_module : RT_STANDIN_4_CALLBACK_MODULE);
    if (engine == NULL || !engine->loaded || callback != engine->base + RT_STANDIN_4_CALLBACK_RVA || third != 0)
        RT_UNIMPLEMENTED(ctx, "stand-in 4 was called with a shape other than the one it answers (any event number, the callback "
                         RT_STANDIN_4_CALLBACK_MODULE "+%#x, third 0): event %#llx, callback %#llx, third %#llx (%s is %s)",
                         (unsigned)RT_STANDIN_4_CALLBACK_RVA, (unsigned long long)event, (unsigned long long)callback,
                         (unsigned long long)third, RT_STANDIN_4_CALLBACK_MODULE,
                         engine && engine->loaded ? "loaded" : "not loaded");
    const unsigned n = atomic_fetch_add(&registrations, 1) + 1;
    rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 4 answered: originsdk.dll!OriginRegisterEventCallback(event %#llx, "
           RT_STANDIN_4_CALLBACK_MODULE "+%#x, 0): result 0, registration number %u counted, the callback's address not kept: "
           "nothing is ever called back (no event of EA's SDK happens here); EA's SDK is not here and did not answer (issue 32)",
           (unsigned long long)event, (unsigned)RT_STANDIN_4_CALLBACK_RVA, n);
    return 0;
}
