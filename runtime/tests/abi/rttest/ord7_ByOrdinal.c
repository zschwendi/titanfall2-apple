/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  a function exported by ordinal is bound by ordinal and by name to the same stub
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM_ORD(rttest, 7, DWORD, ByOrdinal, DWORD x)
{
    return x + 7;
}
