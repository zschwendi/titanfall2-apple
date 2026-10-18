/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/setupapi/nf-setupapi-setupdigetdeviceinterfacedetaila
 * RT-CASES:  a set other than the empty one fails with ERROR_INVALID_HANDLE; the empty set with no interface data fails with ERROR_INVALID_PARAMETER; the detail buffer and RequiredSize are kept
 * RT-TRAPS:  an interface this set never gave out
 */
#include "rt/shim.h"
#include "rt/win/devices.h"

SHIM(setupapi, BOOL, SetupDiGetDeviceInterfaceDetailA, HDEVINFO DeviceInfoSet, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData, PSP_DEVICE_INTERFACE_DETAIL_DATA_A DeviceInterfaceDetailData, DWORD DeviceInterfaceDetailDataSize, PDWORD RequiredSize, LPVOID DeviceInfoData)
{
    (void)DeviceInterfaceDetailData;
    (void)DeviceInterfaceDetailDataSize;
    (void)RequiredSize;
    (void)DeviceInfoData;

    /* This runtime has one device information set, the empty one, and it never gave out an
     * interface, so every other set is invalid. */
    if (DeviceInfoSet != RT_HDEVINFO_EMPTY)
        return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);

    if (!DeviceInterfaceData)
        return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);

    /* An interface structure this set never gave out: what Windows answers is not settled. */
    RT_UNIMPLEMENTED(ctx, "SetupDiGetDeviceInterfaceDetailA: an interface this set never gave out (...)");
}
