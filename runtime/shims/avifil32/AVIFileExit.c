/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/vfw/nf-vfw-avifileexit
 * RT-CASES:  returns after AVIFileInit, with the log line; returns with no AVIFileInit before it, and more often than AVIFileInit was called; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(avifil32, void, AVIFileExit)
{
    /* RT-DOC, read 2026-10-10: "void AVIFileExit();", return value "None"; "The AVIFileExit
     * function exits the AVIFile library and decrements the reference count for the library."
     * It takes nothing and returns nothing, and the page names no failure and no last error.
     *
     * AVIFileInit (the shim beside this one) initialises nothing and keeps no count, so there
     * is nothing to leave and nothing to decrement.  What Windows does with a call that has no
     * AVIFileInit before it is not on the page; here it is the same nothing. */
    rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "avifil32.dll!AVIFileExit",
                "AVIFileExit: there is no AVIFile library to leave");
}
