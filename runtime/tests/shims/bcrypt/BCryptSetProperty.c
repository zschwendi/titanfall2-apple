#include <string.h>

#include "rt/test.h"
#include "rt/bcrypt.h"
#include "rt/win/error.h"
#include "rt/win/file.h"

/* Set writes nothing, so each case reads the result back through the core (rt_bcrypt_get_property),
 * never through the BCryptGetProperty shim.  A fresh AES provider is in CBC mode: a set that
 * succeeded shows as the new name, not as the default. */

TEST(bcrypt, BCryptSetProperty)
{
    BCRYPT_ALG_HANDLE aes = NULL, aes8 = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    UCHAR object[RT_BCRYPT_AES_OBJECT_LENGTH], secret[16], buf[64];
    ULONG n = 0, strength = 0;

    T_CASE("ChainingMode of an AES provider set to GCM with cbInput 32 or 8 succeeds and reads back");
    T_EQ(rt_bcrypt_open_provider(&aes, u"AES", NULL, 0), STATUS_SUCCESS);
    T_EQ(CALL(bcrypt, BCryptSetProperty, aes, u"ChainingMode", (PUCHAR)u"ChainingModeGCM", 32, 0), STATUS_SUCCESS);
    memset(buf, 0xA5, sizeof buf);
    T_EQ(rt_bcrypt_get_property(aes, u"ChainingMode", buf, 64, &n, 0), STATUS_SUCCESS);
    T_EQ(n, 64);
    T_TRUE(memcmp(buf, u"ChainingModeGCM", 32) == 0);
    /* The game passes cbInput 8 (the core logs one WARN line for it); a fresh provider shows the set. */
    T_EQ(rt_bcrypt_open_provider(&aes8, u"AES", NULL, 0), STATUS_SUCCESS);
    T_EQ(CALL(bcrypt, BCryptSetProperty, aes8, u"ChainingMode", (PUCHAR)u"ChainingModeGCM", 8, 0), STATUS_SUCCESS);
    memset(buf, 0xA5, sizeof buf);
    T_EQ(rt_bcrypt_get_property(aes8, u"ChainingMode", buf, 64, &n, 0), STATUS_SUCCESS);
    T_TRUE(memcmp(buf, u"ChainingModeGCM", 32) == 0);

    T_CASE("ChainingMode on a key is STATUS_NOT_SUPPORTED");
    memset(secret, 0x3C, sizeof secret);
    memset(object, 0xA5, sizeof object);
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key, object, sizeof object, secret, 16, 0), STATUS_SUCCESS);
    T_EQ(CALL(bcrypt, BCryptSetProperty, (BCRYPT_HANDLE)key, u"ChainingMode", (PUCHAR)u"ChainingModeGCM", 32, 0), STATUS_NOT_SUPPORTED);
    T_EQ(rt_bcrypt_get_property((BCRYPT_HANDLE)key, u"KeyStrength", (PUCHAR)&strength, 4, &n, 0), STATUS_SUCCESS);
    T_EQ(strength, 128);

    T_CASE("success and failure leave last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(bcrypt, BCryptSetProperty, aes, u"ChainingMode", (PUCHAR)u"ChainingModeGCM", 32, 0), STATUS_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(bcrypt, BCryptSetProperty, (BCRYPT_HANDLE)key, u"ChainingMode", (PUCHAR)u"ChainingModeGCM", 32, 0), STATUS_NOT_SUPPORTED);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Core stops (RT-TRAPS, core:): the shim adds no guard; the core reports them. */
    T_TRAPS(CALL(bcrypt, BCryptSetProperty, aes, u"ChainingMode", (PUCHAR)u"ChainingModeECB", 32, 0), "rt_bcrypt_set_property: not implemented");
    T_TRAPS(CALL(bcrypt, BCryptSetProperty, aes, u"ChainingMode", (PUCHAR)u"ChainingModeGCM", 32, 1), "rt_bcrypt_set_property: not implemented");
}
