/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/previous-versions/bb432254(v=vs.85)
 * RT-CASES:  returns the pointer unchanged; NULL stays NULL; a value with all 64 bits set is returned whole; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, PVOID, EncodePointer, PVOID Ptr)
{
    return Ptr;
}
