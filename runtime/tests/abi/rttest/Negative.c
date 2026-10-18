/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  signed 16-bit and 8-bit arguments are sign-extended from their own width; a negative 32-bit result has a zero upper half in rax
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(rttest, LONG, Negative, SHORT s, CHAR c)
{
    return (LONG)s + (LONG)c;
}
