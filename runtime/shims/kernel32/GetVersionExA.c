/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversionexa
 * RT-CASES:  size 148 fills OSVERSIONINFOA and returns TRUE; size 148 writes nothing past its 148 bytes; size 156 fills OSVERSIONINFOEXA and returns TRUE; the numbers agree with GetVersion; success leaves last error alone
 * RT-TRAPS:  NULL lpVersionInformation; dwOSVersionInfoSize other than 148 or 156
 */
#include "rt/shim.h"
#include "rt/machine.h"
#include "rt/win/sysinfo.h"

SHIM(kernel32, BOOL, GetVersionExA, LPOSVERSIONINFOA lpVersionInformation)
{
    if (!lpVersionInformation) RT_UNIMPLEMENTED(ctx, "GetVersionExA with a NULL lpVersionInformation");

    /* The caller says which structure it passes; the shim reads the size and never writes it. */
    DWORD size = lpVersionInformation->dwOSVersionInfoSize;
    if (size != sizeof(OSVERSIONINFOA) && size != sizeof(OSVERSIONINFOEXA))
        RT_UNIMPLEMENTED(ctx, "GetVersionExA with dwOSVersionInfoSize other than 148 or 156 (%u)", (unsigned)size);

    /* The Windows 7 SP1 numbers, from the machine (machine.h). */
    const RtMachine *m = rt_machine();
    lpVersionInformation->dwMajorVersion = m->os_major;
    lpVersionInformation->dwMinorVersion = m->os_minor;
    lpVersionInformation->dwBuildNumber = m->os_build;
    lpVersionInformation->dwPlatformId = VER_PLATFORM_WIN32_NT;

    /* szCSDVersion: all 128 bytes zero, then the service pack text (at most 127 characters). */
    CHAR *csd = lpVersionInformation->szCSDVersion;
    for (DWORD i = 0; i < 128; i++) csd[i] = 0;
    for (DWORD i = 0; i < 127 && m->sp_name[i]; i++) csd[i] = m->sp_name[i];

    if (size == sizeof(OSVERSIONINFOEXA)) {
        OSVERSIONINFOEXA *ex = (OSVERSIONINFOEXA *)lpVersionInformation;
        ex->wServicePackMajor = m->sp_major;
        ex->wServicePackMinor = m->sp_minor;
        ex->wSuiteMask = VER_SUITE_SINGLEUSERTS;
        ex->wProductType = VER_NT_WORKSTATION;
        ex->wReserved = 0;
    }
    return TRUE;
}
