/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom
 * RT-CASES:  fills exactly cbBuffer bytes with the system-preferred generator; two calls give different bytes; zero-length buffer writes nothing and succeeds; handle must be NULL with the system-preferred flag; without the flag a handle that is no provider is invalid; an RNG provider's handle with dwFlags 0 fills the buffer; NULL buffer with a length is invalid; success leaves last error alone
 * RT-TRAPS:  dwFlags other than BCRYPT_USE_SYSTEM_PREFERRED_RNG; NULL pbBuffer and cbBuffer 0; core: an AES provider's handle
 */
#include "rt/shim.h"
#include "rt/machine.h"
#include "rt/bcrypt.h"
#include "rt/win/devices.h"
#include "rt/win/file.h"

SHIM(bcrypt, NTSTATUS, BCryptGenRandom, BCRYPT_ALG_HANDLE hAlgorithm, PBYTE pbBuffer, ULONG cbBuffer, ULONG dwFlags)
{
    /* Only BCRYPT_USE_SYSTEM_PREFERRED_RNG is handled.  Any other bit, among them
     * BCRYPT_RNG_USE_ENTROPY_IN_BUFFER (0x1), is a case the shim does not implement. */
    if (dwFlags & ~BCRYPT_USE_SYSTEM_PREFERRED_RNG)
        RT_UNIMPLEMENTED(ctx, "BCryptGenRandom with dwFlags other than BCRYPT_USE_SYSTEM_PREFERRED_RNG (0x%x)", (unsigned)dwFlags);
    /* Without the flag the handle must come from BCryptOpenAlgorithmProvider.  A value that
     * did not is no handle.  That includes the pre-defined handle BCRYPT_RNG_ALG_HANDLE (0x81),
     * which Windows 10 takes here with dwFlags 0: Microsoft's page "CNG Algorithm Pseudo-Handles"
     * (learn.microsoft.com/windows/win32/seccng/cng-algorithm-pseudo-handles) says these handles
     * begin in Windows 10, and the machine this runtime describes is Windows 7 SP1
     * (docs/RUNTIME.md section 8.5), where 0x81 is no handle. */
    if (!(dwFlags & BCRYPT_USE_SYSTEM_PREFERRED_RNG)) {
        /* A provider this runtime gave out (rt/bcrypt.h): the engine opens L"RNG" and draws its
         * 16-byte key secrets through that handle with dwFlags 0 (engine.dll+0x215498, five calls
         * at 9.060 s of the recorded start).  The core fills the buffer for an RNG provider and
         * stops for an AES one and for a NULL buffer.  NULL and every other value: no handle. */
        if (RT_BCRYPT_IS_ALG(hAlgorithm)) return rt_bcrypt_gen_random(hAlgorithm, pbBuffer, cbBuffer, dwFlags);
        return STATUS_INVALID_HANDLE;
    }
    /* With the flag the handle must be NULL. */
    if (hAlgorithm != NULL) return STATUS_INVALID_PARAMETER;
    if (pbBuffer == NULL) {
        if (cbBuffer != 0) return STATUS_INVALID_PARAMETER;
        RT_UNIMPLEMENTED(ctx, "BCryptGenRandom with NULL pbBuffer and cbBuffer 0");
    }
    /* The core writes exactly cbBuffer bytes and cannot fail. */
    rt_random_bytes(pbBuffer, cbBuffer);
    return STATUS_SUCCESS;
}
