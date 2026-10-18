#include <stdint.h>

#include "rt/test.h"
#include "rt/win/error.h"
#include "rt/win/file.h"
#include "rt/bcrypt.h"

TEST(bcrypt, BCryptOpenAlgorithmProvider)
{
    BCRYPT_ALG_HANDLE aes = NULL, aes2 = NULL, rng = NULL, prim = NULL;

    T_CASE("AES with NULL implementation and flags 0 gives a new AES provider");
    T_EQ(CALL(bcrypt, BCryptOpenAlgorithmProvider, &aes, u"AES", NULL, 0), STATUS_SUCCESS);
    T_TRUE(RT_BCRYPT_IS_ALG(aes));

    T_CASE("a second AES call gives a different handle");
    T_EQ(CALL(bcrypt, BCryptOpenAlgorithmProvider, &aes2, u"AES", NULL, 0), STATUS_SUCCESS);
    T_TRUE(RT_BCRYPT_IS_ALG(aes2));
    T_TRUE(aes2 != aes);

    T_CASE("RNG with NULL implementation and flags 0 gives a new provider");
    T_EQ(CALL(bcrypt, BCryptOpenAlgorithmProvider, &rng, u"RNG", NULL, 0), STATUS_SUCCESS);
    T_TRUE(RT_BCRYPT_IS_ALG(rng));
    T_TRUE(rng != aes && rng != aes2);

    T_CASE("AES with Microsoft Primitive Provider");
    T_EQ(CALL(bcrypt, BCryptOpenAlgorithmProvider, &prim, u"AES", u"Microsoft Primitive Provider", 0), STATUS_SUCCESS);
    T_TRUE(RT_BCRYPT_IS_ALG(prim));

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(bcrypt, BCryptOpenAlgorithmProvider, &aes, u"AES", NULL, 0), STATUS_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(bcrypt, BCryptOpenAlgorithmProvider, &aes, u"SHA256", NULL, 0), "rt_bcrypt_open_provider: not implemented");
    T_TRAPS(CALL(bcrypt, BCryptOpenAlgorithmProvider, &aes, u"AES", NULL, 8), "rt_bcrypt_open_provider: not implemented");
    T_TRAPS(CALL(bcrypt, BCryptOpenAlgorithmProvider, NULL, u"AES", NULL, 0), "rt_bcrypt_open_provider: not implemented");
}
