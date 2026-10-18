/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/bcrypt/nf-bcrypt-bcryptopenalgorithmprovider
 * RT-CASES:  AES with NULL implementation and flags 0 gives a new AES provider; a second AES call gives a different handle; RNG with NULL implementation and flags 0 gives a new provider; AES with Microsoft Primitive Provider; success leaves last error alone
 * RT-TRAPS:  core: algorithm other than AES and RNG; core: flags other than 0; core: NULL phAlgorithm
 */
#include "rt/shim.h"
#include "rt/bcrypt.h"

SHIM(bcrypt, NTSTATUS, BCryptOpenAlgorithmProvider, BCRYPT_ALG_HANDLE *phAlgorithm, LPCWSTR pszAlgId, LPCWSTR pszImplementation, ULONG dwFlags)
{
    return rt_bcrypt_open_provider(phAlgorithm, pszAlgId, pszImplementation, dwFlags);
}
