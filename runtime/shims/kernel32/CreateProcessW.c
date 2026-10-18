/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, CreateProcessW, LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    (void)lpProcessAttributes;
    (void)lpThreadAttributes;
    (void)bInheritHandles;
    (void)lpEnvironment;
    (void)lpCurrentDirectory;
    (void)lpStartupInfo;
    (void)lpProcessInformation;
    RT_UNIMPLEMENTED(ctx, "CreateProcessW(application %p, command line %p, flags 0x%X); run with RT_LOG=trace to see the strings",
                     (void *)lpApplicationName, (void *)lpCommandLine, (unsigned)dwCreationFlags);
}
