/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  a function without a result writes through its pointer argument
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(rttest, void, Poke, PDWORD where, DWORD value)
{
    *where = value;
}
