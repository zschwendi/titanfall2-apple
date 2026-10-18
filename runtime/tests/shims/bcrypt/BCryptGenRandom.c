#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/win/devices.h"
#include "rt/win/error.h"
#include "rt/win/file.h"
#include "rt/bcrypt.h"

/* The bytes are random, so the checks are about what was written, never values.  Each buffer
 * is GUARD sentinel bytes, the body the call fills, then GUARD sentinel bytes: a call must
 * change exactly the body, and a body of 16 bytes or more cannot stay all sentinel. */
#define GUARD     16
#define SENT      0xA5
#define BODY_MAX  300
#define ENTROPY_IN_BUFFER  0x1u     /* BCRYPT_RNG_USE_ENTROPY_IN_BUFFER: not the system flag */

static size_t count_byte(const uint8_t *p, size_t n, uint8_t v)
{
    size_t k = 0;
    for (size_t i = 0; i < n; i++) k += (p[i] == v);
    return k;
}

TEST(bcrypt, BCryptGenRandom)
{
    static const ULONG sizes[] = { 1, 7, 64, BODY_MAX };
    uint8_t area[GUARD + BODY_MAX + GUARD];
    uint8_t a[32], b[32];

    T_CASE("fills exactly cbBuffer bytes with the system-preferred generator");
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        ULONG n = sizes[i];
        memset(area, SENT, sizeof area);
        T_EQ(CALL(bcrypt, BCryptGenRandom, NULL, area + GUARD, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG), STATUS_SUCCESS);
        T_EQ(count_byte(area, GUARD, SENT), GUARD);
        T_EQ(count_byte(area + GUARD + n, sizeof area - GUARD - n, SENT), sizeof area - GUARD - n);
        if (n >= 16) T_TRUE(count_byte(area + GUARD, n, SENT) < n);
    }

    T_CASE("two calls give different bytes");
    T_EQ(CALL(bcrypt, BCryptGenRandom, NULL, a, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG), STATUS_SUCCESS);
    T_EQ(CALL(bcrypt, BCryptGenRandom, NULL, b, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG), STATUS_SUCCESS);
    T_TRUE(memcmp(a, b, sizeof a) != 0);

    T_CASE("zero-length buffer writes nothing and succeeds");
    memset(area, SENT, sizeof area);
    T_EQ(CALL(bcrypt, BCryptGenRandom, NULL, area + GUARD, 0, BCRYPT_USE_SYSTEM_PREFERRED_RNG), STATUS_SUCCESS);
    T_EQ(count_byte(area, sizeof area, SENT), sizeof area);

    T_CASE("handle must be NULL with the system-preferred flag");
    memset(area, SENT, sizeof area);
    T_EQ(CALL(bcrypt, BCryptGenRandom, (BCRYPT_ALG_HANDLE)area, area + GUARD, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG),
         STATUS_INVALID_PARAMETER);
    T_EQ(count_byte(area, sizeof area, SENT), sizeof area);

    T_CASE("without the flag a handle that is no provider is invalid");
    memset(area, SENT, sizeof area);
    T_EQ(CALL(bcrypt, BCryptGenRandom, NULL, area + GUARD, 16, 0), STATUS_INVALID_HANDLE);
    T_EQ(CALL(bcrypt, BCryptGenRandom, (BCRYPT_ALG_HANDLE)area, area + GUARD, 16, 0), STATUS_INVALID_HANDLE);
    /* 0x81 is BCRYPT_RNG_ALG_HANDLE, a pre-defined handle that needs no BCryptOpenAlgorithmProvider.
     * Windows 10 fills the buffer for it; the page "CNG Algorithm Pseudo-Handles" gives the value
     * and says these handles begin in Windows 10.  The machine here is Windows 7 SP1
     * (docs/RUNTIME.md section 8.5), so 0x81 is refused like any other value. */
    T_EQ(CALL(bcrypt, BCryptGenRandom, (BCRYPT_ALG_HANDLE)(uintptr_t)0x81, area + GUARD, 16, 0), STATUS_INVALID_HANDLE);
    T_EQ(count_byte(area, sizeof area, SENT), sizeof area);

    T_CASE("an RNG provider's handle with dwFlags 0 fills the buffer");
    {
        /* The engine's sequence (the recording, engine.dll+0x215472 and +0x215498): open L"RNG",
         * then 16 bytes through that handle with dwFlags 0.  The provider comes from the core. */
        BCRYPT_ALG_HANDLE rng = NULL, aes = NULL;
        unsigned char a[16], b[16];
        T_EQ(rt_bcrypt_open_provider(&rng, u"RNG", NULL, 0), STATUS_SUCCESS);
        memset(area, SENT, sizeof area);
        T_EQ(CALL(bcrypt, BCryptGenRandom, rng, area + GUARD, 16, 0), STATUS_SUCCESS);
        T_EQ(count_byte(area, GUARD, SENT), GUARD);
        T_EQ(count_byte(area + GUARD + 16, GUARD, SENT), GUARD);
        memcpy(a, area + GUARD, 16);
        T_EQ(CALL(bcrypt, BCryptGenRandom, rng, area + GUARD, 16, 0), STATUS_SUCCESS);
        memcpy(b, area + GUARD, 16);
        T_TRUE(memcmp(a, b, 16) != 0);
        T_EQ(rt_bcrypt_open_provider(&aes, u"AES", NULL, 0), STATUS_SUCCESS);
        T_TRAPS(CALL(bcrypt, BCryptGenRandom, aes, area + GUARD, 16, 0), "rt_bcrypt_gen_random: not implemented: handle");
    }

    T_CASE("NULL buffer with a length is invalid");
    T_EQ(CALL(bcrypt, BCryptGenRandom, NULL, NULL, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG), STATUS_INVALID_PARAMETER);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(bcrypt, BCryptGenRandom, NULL, a, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG), STATUS_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(bcrypt, BCryptGenRandom, NULL, a, 32, ENTROPY_IN_BUFFER), "dwFlags other than BCRYPT_USE_SYSTEM_PREFERRED_RNG");
    T_TRAPS(CALL(bcrypt, BCryptGenRandom, NULL, NULL, 0, BCRYPT_USE_SYSTEM_PREFERRED_RNG), "NULL pbBuffer and cbBuffer 0");
}
