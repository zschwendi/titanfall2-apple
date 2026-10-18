/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/hidsdi/nf-hidsdi-hidd_getattributes
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"
#include "rt/win/devices.h"

SHIM(hid, BOOLEAN, HidD_GetAttributes, HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes)
{
    (void)Attributes;
    RT_UNIMPLEMENTED(ctx, "HidD_GetAttributes on handle %p: this machine has no HID collection",
                     HidDeviceObject);
}
