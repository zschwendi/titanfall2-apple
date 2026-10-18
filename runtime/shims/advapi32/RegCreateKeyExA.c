/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winreg/nf-winreg-regcreatekeyexa
 * RT-CASES:  a new key reports REG_CREATED_NEW_KEY, and the same call again REG_OPENED_EXISTING_KEY; a path of two new components creates both; a path of 32 levels, the most the documentation allows, is created; lpdwDisposition NULL is allowed; an empty subkey is a new handle to the parent itself, and to a predefined parent a handle that is not the predefined one; REG_OPTION_VOLATILE makes the key volatile, and is ignored for a key that exists; REG_OPTION_OPEN_LINK is ignored, and a key created with it is an ordinary key; lpClass is kept with a key that is created, and a call that finds the key does not change it; samDesired is the access of the new handle; samDesired 0 gives a handle that can be closed, flushed and used as a base; a security attributes structure with no descriptor is accepted; result NULL fails with ERROR_INVALID_PARAMETER; a handle that is not open fails with ERROR_INVALID_HANDLE; a parent deleted while open fails with ERROR_KEY_DELETED; a key made directly under HKEY_LOCAL_MACHINE or HKEY_USERS fails with ERROR_ACCESS_DENIED, and an existing one is opened; a non-volatile key below a volatile key fails with ERROR_CHILD_MUST_BE_VOLATILE; a trailing backslash is ignored; a leading backslash fails with ERROR_BAD_PATHNAME, or is skipped under HKEY_CLASSES_ROOT; does not change last error
 * RT-TRAPS:  lpSubKey NULL; core: reserved nonzero; core: REG_OPTION_CREATE_LINK; core: REG_OPTION_BACKUP_RESTORE; core: security descriptor; core: undocumented bit in samDesired; core: predefined parent HKEY_PERFORMANCE_DATA; core: predefined parent HKEY_PERFORMANCE_TEXT; core: predefined parent HKEY_PERFORMANCE_NLSTEXT; core: predefined parent HKEY_CURRENT_USER_LOCAL_SETTINGS; core: doubled backslash; core: a path of backslashes only under HKEY_CLASSES_ROOT; core: a leading backslash below a handle opened under HKEY_CLASSES_ROOT; core: key name longer than 255 characters; core: subkey string longer than 32767 characters
 */
#include "rt/shim.h"
#include "rt/registry.h"

SHIM(advapi32, LSTATUS, RegCreateKeyExA, HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions, REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition)
{
    /* The documentation says lpSubKey cannot be NULL and does not say what Windows does with it;
     * the core would treat it as empty, which is UNSURE (registry.h), so the shim does not pass it on. */
    if (!lpSubKey) RT_UNIMPLEMENTED(ctx, "RegCreateKeyExA with lpSubKey NULL");

    /* Everything else, the fatal cases included, is the core's (registry.h): one call, same order. */
    return rt_reg_create_key_ex_a(hKey, lpSubKey, Reserved, lpClass, dwOptions, samDesired,
                                  lpSecurityAttributes, phkResult, lpdwDisposition);
}
