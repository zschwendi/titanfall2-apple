/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/setupapi/nf-setupapi-setupdienumdeviceinterfaces
 * RT-CASES:  index 0 of the empty set fails with ERROR_NO_MORE_ITEMS; index 3 of the empty set fails with ERROR_NO_MORE_ITEMS; the structure is not written; a set other than the empty one fails with ERROR_INVALID_HANDLE
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/devices.h"

SHIM(setupapi, BOOL, SetupDiEnumDeviceInterfaces, HDEVINFO DeviceInfoSet, LPVOID DeviceInfoData, LPCGUID InterfaceClassGuid, DWORD MemberIndex, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData)
{
    (void)DeviceInfoData;
    (void)InterfaceClassGuid;
    (void)MemberIndex;
    (void)DeviceInterfaceData;

    /* This machine has no HID device, so its one device information set is the empty one: it
     * holds no interface at any index, and the caller's loop ends at once. That end is the
     * normal result, so nothing is logged. */
    if (DeviceInfoSet == RT_HDEVINFO_EMPTY)
        return FAIL(ctx, ERROR_NO_MORE_ITEMS, FALSE);

    /* Any other set is not one this runtime has. */
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
