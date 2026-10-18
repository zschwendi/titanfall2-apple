/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  calls a guest function with mixed integer, float and double arguments and gets a double back
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(rttest, double, CallBackFp, GUESTFN fn, LONG n, double x, float y, double z)
{
    /* Positions: n in rcx, x in xmm1, y in xmm2, the constant in r9, z on the stack. */
    const RtArg args[5] = { RT_ARG_U(n), RT_ARG_D(x), RT_ARG_F(y), RT_ARG_U(1000), RT_ARG_D(z) };
    return rt_guest_call_fp(ctx, fn, args, 5);
}
