/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the observed call (first 0, a pointer to a zero byte, third 0xffffffff): result 0; the byte and everything around it is left as it was; one line is logged for each answer
 * RT-TRAPS:  a first argument that is not 0; a NULL second argument; a third argument that is not 0xffffffff; a byte that is not zero when the call arrives
 */

/* STAND-IN 2 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * It is not in the runtime's shim table (rt_shim_table): it is the one row of rt_standin_table
 * (rt/standin.h), and a program has it only if it hands that row to rt_init itself.  `gate3
 * run` does, unless GATE3_NO_STANDIN_PROGRESSIVE_INSTALL=1 (gate3/run.c).  In every other
 * program, and for every other import of originsdk.dll in every program, the import is the
 * stub that halts the run.
 *
 * What it is: the one answer, posted on issue 32 on 11 October 2026 before this file was
 * written (RT-DOC above; the owner's rule is the plan's answer 2.1), to the engine's call
 *
 *     originsdk.dll!OriginIsProgressiveInstallationAvailableSync
 *
 * made at engine+0x695c on a thread of its own (finding 37, step 29;
 * docs/handoff/gate3-run-4.md, section 4).  The answer is: result 0, and the byte the second
 * argument points to left as the engine set it (zero): "the call worked; progressive
 * installation is not available".  That is a fact of this installation (the game is complete
 * on disk and nothing is downloading it).  It is no identity, no entitlement, no
 * authorisation code and no setting.
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY.  OriginSDK.dll was not
 * read, disassembled, lifted or run.  The engine's code around the call (engine+0x6946 to
 * +0x6971; tools/scout/flow_show.py engine 0x6930) writes a zero byte to its stack, passes 0
 * in rcx (`xor ecx, ecx`), the byte's address in rdx and 0xffffffff in r8 (`or r8d, -1`),
 * and afterwards tests eax and then the byte: a non-zero result, or a zero result with the
 * byte still zero, and the function returns (the thread ends); only a zero result with a
 * non-zero byte goes on, to three more imports of the same DLL.  Those three stay halting
 * stubs, which is why a byte that is not zero on arrival is a stop here: this answer would
 * send the engine on to them.
 *
 * The types of the three arguments are not known from that side.  All three instructions
 * define the whole 64-bit register, and the stop report of the unanswered call showed
 * rcx=0, r8=00000000ffffffff, so the three are compared as whole registers: anything else is
 * "another shape" and stops with the three printed.  The fourth register is not an argument
 * as far as the engine's code shows (it is not set for the call) and is not read.
 *
 * It reads its three arguments and the one byte, and writes nothing. */
#include "rt/shim.h"

SHIM(originsdk, DWORD, OriginIsProgressiveInstallationAvailableSync, ULONG_PTR first, BYTE *available, ULONG_PTR third)
{
    if (first != 0 || available == NULL || third != 0xffffffffu)
        RT_UNIMPLEMENTED(ctx, "stand-in 2 was called with a shape other than the one it answers (first 0, a non-NULL second, "
                         "third 0xffffffff): first %#llx, second %#llx, third %#llx",
                         (unsigned long long)first, (unsigned long long)(uintptr_t)available, (unsigned long long)third);
    if (*available != 0)
        RT_UNIMPLEMENTED(ctx, "stand-in 2 was called with the byte at its second argument already %#x, not 0 (with this answer "
                         "the engine would go on to three more imports of originsdk.dll): first %#llx, second %#llx, third %#llx",
                         (unsigned)*available, (unsigned long long)first, (unsigned long long)(uintptr_t)available,
                         (unsigned long long)third);
    rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 2 answered: originsdk.dll!OriginIsProgressiveInstallationAvailableSync(0, %#llx, 0xffffffff): "
           "result 0, the caller's byte left at 0 (\"the call worked; progressive installation is not available\"); "
           "EA's SDK is not here and did not answer (issue 32)", (unsigned long long)(uintptr_t)available);
    return 0;
}
