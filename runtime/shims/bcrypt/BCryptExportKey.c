/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/bcrypt/nf-bcrypt-bcryptexportkey
 * RT-CASES:  exports a key as an opaque key blob into the game's buffer, leaving the rest of it; exports a key as a key data blob; a buffer under the size is STATUS_BUFFER_TOO_SMALL with the size stored and nothing written; success and failure leave last error alone
 * RT-TRAPS:  core: blob type AesWrapKeyBlob; core: an export key; core: NULL pbOutput
 */
#include "rt/shim.h"
#include "rt/bcrypt.h"

SHIM(bcrypt, NTSTATUS, BCryptExportKey, BCRYPT_KEY_HANDLE hKey, BCRYPT_KEY_HANDLE hExportKey, LPCWSTR pszBlobType, PUCHAR pbOutput, ULONG cbOutput, ULONG *pcbResult, ULONG dwFlags)
{
    /* The core does all of it: its comment in rt/bcrypt.h is the specification, and it
     * stops for a NULL pbOutput, an export key, and the other cases listed there. */
    return rt_bcrypt_export_key(hKey, hExportKey, pszBlobType, pbOutput, cbOutput, pcbResult, dwFlags);
}
