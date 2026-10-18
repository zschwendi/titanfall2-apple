#include <stdint.h>

#include "rt/test.h"
#include "rt/win/error.h"
#include "rt/bcrypt.h"

TEST(bcrypt, BCryptDestroyKey)
{
    BCRYPT_ALG_HANDLE aes = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    uint8_t object[RT_BCRYPT_AES_OBJECT_LENGTH];
    uint8_t secret[16] = { 0 };

    T_EQ(rt_bcrypt_open_provider(&aes, u"AES", NULL, 0), STATUS_SUCCESS);
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key, object, sizeof object, secret, 16, 0), STATUS_SUCCESS);
    T_TRUE(RT_BCRYPT_IS_KEY(key));

    T_CASE("destroys a live key and succeeds");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(bcrypt, BCryptDestroyKey, key), STATUS_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("NULL key is invalid handle");
    T_EQ(CALL(bcrypt, BCryptDestroyKey, NULL), STATUS_INVALID_HANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("success and failure leave last error alone");
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key, object, sizeof object, secret, 16, 0), STATUS_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(bcrypt, BCryptDestroyKey, key), STATUS_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(bcrypt, BCryptDestroyKey, NULL), STATUS_INVALID_HANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(bcrypt, BCryptDestroyKey, key), "rt_bcrypt_destroy_key: not implemented");
}
