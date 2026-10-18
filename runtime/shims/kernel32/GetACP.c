/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-getacp
 * RT-CASES:  returns the ANSI code page of the en-US machine (1252); does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, UINT, GetACP)
{
    /* The core's ANSI code page: 1252, read from the machine description. */
    return rt_cp_acp();
}
