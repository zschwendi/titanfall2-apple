/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, CreateProcessA, LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    (void)lpProcessAttributes;
    (void)lpThreadAttributes;
    (void)bInheritHandles;
    (void)lpEnvironment;
    (void)lpCurrentDirectory;
    (void)lpStartupInfo;
    (void)lpProcessInformation;
    RT_UNIMPLEMENTED(ctx, "CreateProcessA(application \"%s\", command line \"%s\", flags 0x%X)",
                     lpApplicationName ? lpApplicationName : "(null)",
                     lpCommandLine ? lpCommandLine : "(null)", (unsigned)dwCreationFlags);
}
