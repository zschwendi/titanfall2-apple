/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  a 24-byte struct is returned through the hidden pointer and every argument moves up one position; a 24-byte struct argument arrives as a pointer to the caller's copy
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rttest.h"

SHIM(rttest, RTTEST_BIG, MakeBig, ULONGLONG a, double b, DWORD c, RTTEST_BIG in)
{
    /* With the hidden pointer in rcx: a is in rdx, b in xmm2, c in r9, `in` on the stack. */
    RTTEST_BIG r = { a + in.a, b + in.b, c + in.c, in.d };
    return r;
}
