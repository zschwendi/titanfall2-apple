/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-mapvirtualkeyexa
 * RT-CASES:  map type 0 gives the scan code of a virtual key; map types 1 and 3 give the virtual key of a scan code; map type 2 gives the unshifted character; does not change last error
 * RT-TRAPS:  core: another layout; core: map type 4; core: a key the US table does not have; core: a scan code the US table does not have
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, UINT, MapVirtualKeyExA, UINT uCode, UINT uMapType, HKL dwhkl)
{
    /* The table and every stop are the core's (rt_key_map; docs/handoff/rt-contract-input.md
     * K34 to K37): a code the table does not have is never answered with 0. */
    return rt_key_map(uCode, uMapType, dwhkl);
}
