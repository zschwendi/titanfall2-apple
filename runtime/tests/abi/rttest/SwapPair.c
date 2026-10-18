/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  structs of 8 and 2 bytes are passed and returned by value in a register
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rttest.h"

SHIM(rttest, RTTEST_PAIR, SwapPair, RTTEST_PAIR p, RTTEST_WORD w)
{
    RTTEST_PAIR r = { p.hi + w.w, p.lo };
    return r;
}
