#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/win/error.h"
#include "rt/bcrypt.h"

#define SENT  0xA5

static int all_byte(const uint8_t *p, size_t n, uint8_t v)
{
    for (size_t i = 0; i < n; i++) if (p[i] != v) return 0;
    return 1;
}

TEST(bcrypt, BCryptGenerateSymmetricKey)
{
    BCRYPT_ALG_HANDLE aes = NULL;
    uint8_t object[RT_BCRYPT_AES_OBJECT_LENGTH];
    uint8_t secret[32];
    BCRYPT_KEY_HANDLE key = NULL;
    static const ULONG lens[] = { 16, 24, 32 };

    memset(secret, 0x3C, sizeof secret);
    T_EQ(rt_bcrypt_open_provider(&aes, u"AES", NULL, 0), STATUS_SUCCESS);

    T_CASE("a 16, 24 or 32-byte secret on an AES provider gives a key and the object buffer is not written");
    for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        memset(object, SENT, sizeof object);
        key = NULL;
        T_EQ(CALL(bcrypt, BCryptGenerateSymmetricKey, aes, &key, object, sizeof object, secret, lens[i], 0), STATUS_SUCCESS);
        T_TRUE(RT_BCRYPT_IS_KEY(key));
        T_TRUE(all_byte(object, sizeof object, SENT));
        T_EQ(rt_bcrypt_destroy_key(key), STATUS_SUCCESS);
    }

    T_CASE("NULL provider is invalid handle and leaves the key alone");
    memset(object, SENT, sizeof object);
    key = (BCRYPT_KEY_HANDLE)(uintptr_t)0x1111;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(bcrypt, BCryptGenerateSymmetricKey, NULL, &key, object, sizeof object, secret, 16, 0), STATUS_INVALID_HANDLE);
    T_EQ((uintptr_t)key, 0x1111);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a secret of 1 or 17 bytes is invalid parameter and leaves the key alone");
    T_EQ(CALL(bcrypt, BCryptGenerateSymmetricKey, aes, &key, object, sizeof object, secret, 1, 0), STATUS_INVALID_PARAMETER);
    T_EQ((uintptr_t)key, 0x1111);
    T_EQ(CALL(bcrypt, BCryptGenerateSymmetricKey, aes, &key, object, sizeof object, secret, 17, 0), STATUS_INVALID_PARAMETER);
    T_EQ((uintptr_t)key, 0x1111);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("an object buffer under 654 bytes is too small and leaves the key alone");
    T_EQ(CALL(bcrypt, BCryptGenerateSymmetricKey, aes, &key, object, RT_BCRYPT_AES_OBJECT_LENGTH - 1, secret, 16, 0),
         STATUS_BUFFER_TOO_SMALL);
    T_EQ((uintptr_t)key, 0x1111);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("success leaves last error alone");
    key = NULL;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(bcrypt, BCryptGenerateSymmetricKey, aes, &key, object, sizeof object, secret, 16, 0), STATUS_SUCCESS);
    T_TRUE(RT_BCRYPT_IS_KEY(key));
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_bcrypt_destroy_key(key), STATUS_SUCCESS);

    T_TRAPS(CALL(bcrypt, BCryptGenerateSymmetricKey, aes, &key, object, sizeof object, secret, 20, 0),
            "rt_bcrypt_generate_symmetric_key: not implemented");
}
