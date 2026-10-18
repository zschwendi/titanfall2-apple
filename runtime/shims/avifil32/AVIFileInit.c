/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/vfw/nf-vfw-avifileinit
 * RT-CASES:  returns, with the log line; a second and a third call return too and log nothing more; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(avifil32, void, AVIFileInit)
{
    /* RT-DOC, read 2026-10-10: "void AVIFileInit();", return value "None"; "The AVIFileInit
     * function initializes the AVIFile library.  The AVIFile library maintains a count of the
     * number of times it is initialized, but not the number of times it was released."  It
     * takes nothing and returns nothing, and the page names no failure and no last error.
     *
     * This machine has no AVIFile library to initialise (the planner's decision, brief of unit
     * gate3-run-3): the 15 other functions the game's DLLs import from avifil32.dll have no
     * shim, so a call of any of them halts the run with its name and call site, which is where
     * a file or stream would first be needed.  The count the page speaks of is not kept: its
     * only reader is the library itself.  The game calls this once (the call returns to engine.dll+0x417ec) on
     * the main thread before it makes its window (2.853 s in the recorded start, finding 39). */
    rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "avifil32.dll!AVIFileInit",
                "AVIFileInit: there is no AVIFile library; every other function of avifil32.dll is a stop");
}
