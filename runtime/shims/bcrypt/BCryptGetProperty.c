/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/bcrypt/nf-bcrypt-bcryptgetproperty
 * RT-CASES:  ObjectLength of an AES provider; BlockLength of an AES provider; ChainingMode of an AES provider; ChainingMode with a 63-byte buffer; KeyStrength of a key; success and failure leave last error alone
 * RT-TRAPS:  core: get of HashDigestLength; core: get with dwFlags 1
 */
#include "rt/shim.h"
#include "rt/bcrypt.h"

SHIM(bcrypt, NTSTATUS, BCryptGetProperty, BCRYPT_HANDLE hObject, LPCWSTR pszProperty, PUCHAR pbOutput, ULONG cbOutput, ULONG *pcbResult, ULONG dwFlags)
{
    /* The core decides every case, its stops included (RT-TRAPS, core:).  It never touches
     * last error, and neither does the shim. */
    return rt_bcrypt_get_property(hObject, pszProperty, pbOutput, cbOutput, pcbResult, dwFlags);
}
