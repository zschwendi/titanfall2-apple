/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  integer, float and double arguments in registers and on the stack; narrow integers with garbage above them
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(rttest, double, MixArgs, DWORD a, float b, double c, LONG d, float e, double f, ULONGLONG g, WORD h, BYTE i, SHORT j)
{
    /* Arguments 0-3 come from rcx/xmm1/xmm2/r9 (chosen by position), 4-9 from the stack. */
    return (double)a + 2.0 * (double)b + 3.0 * c + 4.0 * (double)d + 5.0 * (double)e + 6.0 * f
         + 7.0 * (double)g + 8.0 * (double)h + 9.0 * (double)i + 10.0 * (double)j;
}
