/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/setupapi/nf-setupapi-setupidestroydeviceinfolist
 * RT-CASES:  returns TRUE for the empty device information set; sets ERROR_INVALID_HANDLE for NULL and for INVALID_HANDLE_VALUE; sets ERROR_INVALID_HANDLE for any other value
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/devices.h"

SHIM(setupapi, BOOL, SetupDiDestroyDeviceInfoList, HDEVINFO DeviceInfoSet)
{
    /* The empty set is the only device information set this runtime has: destroying it
     * succeeds.  Any other value is not a device information set here. */
    if (DeviceInfoSet == RT_HDEVINFO_EMPTY) return TRUE;
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
