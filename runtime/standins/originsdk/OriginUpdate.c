/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's per-frame call (no argument, from engine+0x18490f): result 0; nothing is written and nothing is called back; every answer is counted; one line is logged for the first answer and none for the answers after it; when stand-in 7 accepted a request, the answer after it delivers the profile once
 * RT-TRAPS:  a call from anywhere but engine+0x18490f (the return address is not engine.dll+0x184914); a call while engine.dll is not loaded
 */

/* STAND-IN 5 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * It is not in the runtime's shim table (rt_shim_table): it is a row of rt_standin_table
 * (rt/standin.h), and a program has it only if it hands that row to rt_init itself.  `gate3
 * run` does, unless GATE3_NO_STANDIN_UPDATE=1 (gate3/run.c).  In every other program, and for
 * every import of originsdk.dll that has no stand-in, the import is the stub that halts the
 * run.
 *
 * What it is: the one answer, posted on issue 32 on 11 October 2026 before this file was
 * written (RT-DOC above; the owner's words of that day: "no user is fine to do, so go for
 * that"), to the engine's call
 *
 *     originsdk.dll!OriginUpdate()
 *
 * made at engine+0x18490f on the main thread in the engine's per-frame function for EA's kit
 * (engine+0x184890), once in every frame of the main loop.  The answer is: RESULT 0 ("the kit
 * has no error this frame").  IT WRITES NOTHING AND CALLS NOTHING BACK.  It gives the engine
 * nothing: no identity, no user or persona id, no entitlement, no authorisation code, nothing
 * of an account, no setting.
 *
 * BUILT AFTER THE RUN STOOD THERE.  docs/handoff/gate3-run-7.md, section 5, entry 2, and
 * again on this unit's tree before this file existed:
 *     FATAL unimplemented import originsdk.dll!OriginUpdate
 *       called from engine.dll+0x184914   thread 0x1004 ""
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY.  OriginSDK.dll was not
 * read, disassembled, lifted or run.  The engine's code (tools/scout/flow_show.py engine.dll
 * 0x184890 --all --from 0x184900 --to 0x184920) sets up no argument, makes the call at
 * +0x18490f (five bytes: the next instruction is at +0x184914), and reads the low half of the
 * result: 0 goes on, anything else is the game's own fatal error.  The import has that one
 * call site in the game's DLLs (flow_refs.py engine.dll imp '^OriginUpdate$': 1 site).
 *
 * THE STOP.  The post names the call: "the engine's per-frame call at engine+0x18490f, with
 * no argument".  There is no argument to compare, so the stand-in compares where the call
 * came from: the return address on the guest stack must be engine.dll's base + 0x184914.  A
 * call that arrives any other way (a function pointer some other code got by name, another
 * module) is not the posted call and halts.  A call while engine.dll is not loaded stops too.
 *
 * SINCE THE PLACEHOLDER USER (issue 32, comments 6114036238 and 6114239628): "Stand-in 5
 * (OriginUpdate) changes in one way: it now delivers that one profile callback.  It still
 * returns 0."  The kit's per-frame turn is where a kit would call back, so each answer first
 * asks stand-in 7 (rt_standin7_deliver, OriginGetProfile.c) to deliver the profile it
 * accepted a request for, if there is one: once in a run, on this thread, and only when the
 * program selected the placeholder user (stand-in 7 accepts no request otherwise).  With the
 * user "nobody" stand-in 5 is exactly what it was.
 *
 * BOTH ARE CALLED EVERY FRAME (this one and stand-in 6): the first answer is logged, the
 * answers after it are only counted (rt_standin5_answers); `gate3 run` prints the count at
 * every end of a run.
 *
 * It reads the return address and writes nothing to guest memory.
 *
 * rt_standin56_test_module and rt_standin56_test_return_rva (rt/standin.h) are NULL and 0 in
 * every program; the tests of stand-ins 5 and 6, whose runtime has no engine.dll and whose
 * calls return to the test harness, name there the module and the rva their calls return to. */
#include <stdatomic.h>

#include "rt/shim.h"
#include "rt/standin.h"

const char *rt_standin56_test_module;
uint32_t    rt_standin56_test_return_rva;

static _Atomic unsigned long long answers;

unsigned long long rt_standin5_answers(void) { return atomic_load(&answers); }

bool rt_standin_site(Ctx *ctx, uint32_t return_rva, uint64_t *ret, const RtModule **engine)
{
    const char *const name = rt_standin56_test_module ? rt_standin56_test_module : RT_STANDIN_56_CALLER_MODULE;
    const uint32_t rva = rt_standin56_test_module ? rt_standin56_test_return_rva : return_rva;
    const RtModule *m = rt_module_find(name);
    *ret = LD64(ctx->r[4]);                             /* the guest's return address: rsp points at it on entry */
    *engine = m;
    return m != NULL && m->loaded && *ret == m->base + rva;
}

SHIM(originsdk, DWORD, OriginUpdate)
{
    const char *const name = rt_standin56_test_module ? rt_standin56_test_module : RT_STANDIN_56_CALLER_MODULE;
    uint64_t ret;
    const RtModule *engine;
    if (!rt_standin_site(ctx, RT_STANDIN_5_RETURN_RVA, &ret, &engine))
        RT_UNIMPLEMENTED(ctx, "stand-in 5 was called from somewhere other than the one call it answers (the engine's per-frame call at "
                         RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to +%#x): the return address is %#llx (%s is %s)",
                         (unsigned)RT_STANDIN_5_CALL_RVA, (unsigned)RT_STANDIN_5_RETURN_RVA, (unsigned long long)ret,
                         name, engine && engine->loaded ? "loaded" : "not loaded");
    if (atomic_fetch_add(&answers, 1) == 0)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 5 answered: originsdk.dll!OriginUpdate() from " RT_STANDIN_56_CALLER_MODULE "+%#x: "
               "result 0 (\"the kit has no error this frame\"), nothing written and nothing called back; this is its first answer: the "
               "call comes every frame, the answers after this one are counted and not logged; EA's SDK is not here and did not answer "
               "(issue 32)", (unsigned)RT_STANDIN_5_CALL_RVA);
    rt_standin7_deliver(ctx);           /* THE ONE THING STAND-IN 5 DOES BESIDES ANSWERING (the comment above): nothing unless stand-in 7 accepted a request */
    return 0;
}
