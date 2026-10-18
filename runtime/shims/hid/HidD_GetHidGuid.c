/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidsdi/nf-hidsdi-hidd_gethidguid
 * RT-CASES:  stores the device interface GUID of HIDClass devices in the caller's buffer; does not change last error
 * RT-TRAPS:  NULL HidGuid
 */
#include "rt/shim.h"
#include "rt/win/devices.h"

SHIM(hid, void, HidD_GetHidGuid, LPGUID HidGuid)
{
    /* The device interface class of HID collections (rt/win/devices.h). */
    GUID g = RT_GUID_DEVINTERFACE_HID;

    if (!HidGuid) RT_UNIMPLEMENTED(ctx, "HidD_GetHidGuid with NULL HidGuid");
    *HidGuid = g;
}
