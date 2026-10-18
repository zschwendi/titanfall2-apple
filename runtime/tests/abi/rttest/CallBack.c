/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  calls a guest function with six integer arguments through GUEST_CALL and returns its result
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(rttest, ULONGLONG, CallBack, GUESTFN fn, ULONGLONG a, ULONGLONG b, ULONGLONG c, ULONGLONG d, ULONGLONG e, ULONGLONG f)
{
    ULONGLONG before = a ^ b ^ c ^ d ^ e ^ f;
    ULONGLONG r = GUEST_CALL(ctx, fn, a, b, c, d, e, f);
    /* After a guest call the volatile guest registers are garbage; C locals are intact. */
    if ((a ^ b ^ c ^ d ^ e ^ f) != before) return 0;
    return r;
}
