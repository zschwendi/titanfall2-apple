/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/bcrypt/nf-bcrypt-bcryptgeneratesymmetrickey
 * RT-CASES:  a 16, 24 or 32-byte secret on an AES provider gives a key and the object buffer is not written; NULL provider is invalid handle and leaves the key alone; a secret of 1 or 17 bytes is invalid parameter and leaves the key alone; an object buffer under 654 bytes is too small and leaves the key alone; success leaves last error alone
 * RT-TRAPS:  core: secret of 20 bytes
 */
#include "rt/shim.h"
#include "rt/bcrypt.h"

SHIM(bcrypt, NTSTATUS, BCryptGenerateSymmetricKey, BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_KEY_HANDLE *phKey, PUCHAR pbKeyObject, ULONG cbKeyObject, PUCHAR pbSecret, ULONG cbSecret, ULONG dwFlags)
{
    /* The core checks the arguments in Windows' order, and its comment says which cases it
     * stops on: a secret of 20 bytes is one of them, and the shim adds no guard for it. */
    return rt_bcrypt_generate_symmetric_key(hAlgorithm, phKey, pbKeyObject, cbKeyObject, pbSecret, cbSecret, dwFlags);
}
