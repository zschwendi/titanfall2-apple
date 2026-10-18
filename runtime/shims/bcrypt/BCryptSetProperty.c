/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/bcrypt/nf-bcrypt-bcryptsetproperty
 * RT-CASES:  ChainingMode of an AES provider set to GCM with cbInput 32 or 8 succeeds and reads back; ChainingMode on a key is STATUS_NOT_SUPPORTED; success and failure leave last error alone
 * RT-TRAPS:  core: set of ChainingModeECB; core: set with dwFlags 1
 */
#include "rt/shim.h"
#include "rt/bcrypt.h"

SHIM(bcrypt, NTSTATUS, BCryptSetProperty, BCRYPT_HANDLE hObject, LPCWSTR pszProperty, PUCHAR pbInput, ULONG cbInput, ULONG dwFlags)
{
    /* The core decides every case, its stops included (RT-TRAPS, core:).  cbInput is the
     * core's business: the game passes 8 for a 32-byte string.  It never touches last error,
     * and neither does the shim. */
    return rt_bcrypt_set_property(hObject, pszProperty, pbInput, cbInput, dwFlags);
}
