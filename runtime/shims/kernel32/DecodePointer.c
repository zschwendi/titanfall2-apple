/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/previous-versions/bb432242(v=vs.85)
 * RT-CASES:  returns the pointer unchanged; NULL stays NULL; DecodePointer of EncodePointer gives back the pointer; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, PVOID, DecodePointer, PVOID Ptr)
{
    return Ptr;
}
