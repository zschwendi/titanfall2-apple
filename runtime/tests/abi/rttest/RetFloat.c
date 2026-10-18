/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  a float result comes back in xmm0
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(rttest, float, RetFloat, float x, LONG n)
{
    return x * (float)n;
}
