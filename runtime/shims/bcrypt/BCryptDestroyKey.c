/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/bcrypt/nf-bcrypt-bcryptdestroykey
 * RT-CASES:  destroys a live key and succeeds; NULL key is invalid handle; success and failure leave last error alone
 * RT-TRAPS:  core: destroy of a key destroyed before
 */
#include "rt/shim.h"
#include "rt/bcrypt.h"

SHIM(bcrypt, NTSTATUS, BCryptDestroyKey, BCRYPT_KEY_HANDLE hKey)
{
    /* The core answers NULL with STATUS_INVALID_HANDLE and stops on a key destroyed before. */
    return rt_bcrypt_destroy_key(hKey);
}
