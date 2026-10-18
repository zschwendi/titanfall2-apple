/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/shlwapi/nf-shlwapi-shdeletekeya
 * RT-CASES:  deletes a key with two subkeys, so opening it afterwards fails with ERROR_FILE_NOT_FOUND; removes the values of the keys it deletes; a handle open to a key of the tree is ERROR_KEY_DELETED and closes with ERROR_SUCCESS; deletes a key without subkeys; deleting it again fails with ERROR_FILE_NOT_FOUND; a handle that is not open fails with ERROR_INVALID_HANDLE; a preset last error (0x1234) stays on HKEY_CURRENT_USER, on success and on failure
 * RT-TRAPS:  core: a NULL subkey; core: an empty subkey; core: a seeded key in the tree (HKEY_LOCAL_MACHINE "HARDWARE"); core: a key name longer than 255 characters; core: a subkey string longer than 32,767 characters; core: a leading backslash; core: a trailing backslash; core: two backslashes together
 */
#include "rt/shim.h"
#include "rt/registry.h"

SHIM(shlwapi, LSTATUS, SHDeleteKeyA, HKEY hkey, LPCSTR pszSubKey)
{
    /* The core does all of it (rt_reg_delete_tree_a, rt/registry.h): it checks every key of the
     * tree first, then deletes the tree at once and saves the file once, and it returns the
     * status itself.  The registry functions never touch last error, so the shim sets nothing.
     *
     * A NULL or empty subkey, a seeded key in the tree and a bad subkey string stop the runtime
     * in the core (its comment says fatal).  The shim adds no guard for them. */
    return rt_reg_delete_tree_a(hkey, pszSubKey);
}
