/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winreg/nf-winreg-regqueryvalueexa
 * RT-CASES:  a REG_SZ value comes back with its type, its data and its size; a REG_DWORD value comes back with its type, its data and a size of 4; a size query with NULL data gives the size and copies nothing; a buffer too small gives ERROR_MORE_DATA with the size needed and the type, and leaves the buffer alone; a value that does not exist fails with ERROR_FILE_NOT_FOUND and leaves the outputs alone; a handle that is not a key fails with ERROR_INVALID_HANDLE and leaves the outputs alone; a key without KEY_QUERY_VALUE fails with ERROR_ACCESS_DENIED; a reserved pointer that is not NULL, or data without a size, fails with ERROR_INVALID_PARAMETER; does not change last error, on success or failure
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/registry.h"

SHIM(advapi32, LSTATUS, RegQueryValueExA, HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    /* The core does all of it (rt_reg_query_value_ex_a, rt/registry.h): the argument checks, the
     * conversion of the name and of the data of a string type to code page 1252, the size rules
     * and the status it returns, which is the status RegQueryValueExA returns.  The core never
     * touches last error, so success and failure leave it alone. */
    return rt_reg_query_value_ex_a(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}
