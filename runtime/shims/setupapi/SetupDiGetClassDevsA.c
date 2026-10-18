/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/setupapi/nf-setupapi-setupigetclassdevsa
 * RT-CASES:  returns RT_HDEVINFO_EMPTY for the HID class with flags 0x12, on two calls in a row; logs once that no device of the class is there; leaves last error alone
 * RT-TRAPS:  ClassGuid NULL; Enumerator not NULL; a bit outside DIGCF_PRESENT and DIGCF_DEVICEINTERFACE; without DIGCF_DEVICEINTERFACE
 */
#include "rt/shim.h"
#include "rt/win/devices.h"

SHIM(setupapi, HDEVINFO, SetupDiGetClassDevsA, LPCGUID ClassGuid, LPCSTR Enumerator, HWND hwndParent, DWORD Flags)
{
    (void)hwndParent;

    /* Only the query for present interfaces of one class is answered: no device of that class
     * exists here, so the set is empty.  Every other query is refused, with the values given. */
    if (!ClassGuid)
        RT_UNIMPLEMENTED(ctx, "SetupDiGetClassDevsA with ClassGuid NULL (Enumerator %p, Flags 0x%x)",
                         (const void *)Enumerator, (unsigned)Flags);
    if (Enumerator)
        RT_UNIMPLEMENTED(ctx, "SetupDiGetClassDevsA with Enumerator not NULL (Enumerator %p, ClassGuid %p, Flags 0x%x)",
                         (const void *)Enumerator, (const void *)ClassGuid, (unsigned)Flags);
    if (Flags & ~(DIGCF_PRESENT | DIGCF_DEVICEINTERFACE))
        RT_UNIMPLEMENTED(ctx, "SetupDiGetClassDevsA with Flags 0x%x: a bit outside DIGCF_PRESENT and DIGCF_DEVICEINTERFACE (ClassGuid %p)",
                         (unsigned)Flags, (const void *)ClassGuid);
    if (!(Flags & DIGCF_DEVICEINTERFACE))
        RT_UNIMPLEMENTED(ctx, "SetupDiGetClassDevsA with Flags 0x%x without DIGCF_DEVICEINTERFACE (ClassGuid %p)",
                         (unsigned)Flags, (const void *)ClassGuid);

    /* The class GUID in its text form {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}, upper case. */
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "setupapi.dll!SetupDiGetClassDevsA",
                "SetupDiGetClassDevsA: no device of class {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                (unsigned)ClassGuid->Data1, (unsigned)ClassGuid->Data2, (unsigned)ClassGuid->Data3,
                (unsigned)ClassGuid->Data4[0], (unsigned)ClassGuid->Data4[1],
                (unsigned)ClassGuid->Data4[2], (unsigned)ClassGuid->Data4[3],
                (unsigned)ClassGuid->Data4[4], (unsigned)ClassGuid->Data4[5],
                (unsigned)ClassGuid->Data4[6], (unsigned)ClassGuid->Data4[7]);
    return RT_HDEVINFO_EMPTY;
}
