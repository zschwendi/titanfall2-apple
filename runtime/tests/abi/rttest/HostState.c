/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  a shim runs with the host float mode and the in-host mark set
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(rttest, ULONGLONG, HostState)
{
    /* Bit 63: the thread's in-host mark.  Bit 62: this shim is the innermost shim frame.
     * Low bits: FPCR as host code sees it (0 is the host default). */
    ULONGLONG v = tf_fpcr_read() & 0x0FFFFFFFFFFFFFFF;
    if (RT_THREAD(ctx)->in_host) v |= 0x8000000000000000;
    if (rt_current_shim(ctx) && !strcmp(rt_current_shim(ctx)->name, "HostState")) v |= 0x4000000000000000;
    return v;
}
