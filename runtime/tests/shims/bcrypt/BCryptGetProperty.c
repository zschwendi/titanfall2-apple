#include <string.h>

#include "rt/test.h"
#include "rt/bcrypt.h"
#include "rt/win/error.h"
#include "rt/win/file.h"

/* Mode names are wide strings (u"...", 16-bit WCHAR).  A fresh AES provider is in CBC mode, and
 * the 32 bytes of the name include its terminator.  The 32 bytes of a 64-byte answer after the
 * name are not checked: what Windows puts there was not read. */

static int all_bytes(const UCHAR *p, size_t n, UCHAR v)
{
    for (size_t i = 0; i < n; i++) if (p[i] != v) return 0;
    return 1;
}

TEST(bcrypt, BCryptGetProperty)
{
    BCRYPT_ALG_HANDLE aes = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    UCHAR object[RT_BCRYPT_AES_OBJECT_LENGTH], secret[16], buf[64];
    ULONG n = 0, len = 0, blockLength = 0, strength = 0;

    T_CASE("ObjectLength of an AES provider");
    T_EQ(rt_bcrypt_open_provider(&aes, u"AES", NULL, 0), STATUS_SUCCESS);
    T_EQ(CALL(bcrypt, BCryptGetProperty, aes, u"ObjectLength", (PUCHAR)&len, 4, &n, 0), STATUS_SUCCESS);
    T_EQ(len, RT_BCRYPT_AES_OBJECT_LENGTH);
    T_EQ(n, 4);

    T_CASE("BlockLength of an AES provider");
    T_EQ(CALL(bcrypt, BCryptGetProperty, aes, u"BlockLength", (PUCHAR)&blockLength, 4, &n, 0), STATUS_SUCCESS);
    T_EQ(blockLength, 16);
    T_EQ(n, 4);

    T_CASE("ChainingMode of an AES provider");
    memset(buf, 0xA5, sizeof buf);
    T_EQ(CALL(bcrypt, BCryptGetProperty, aes, u"ChainingMode", buf, 64, &n, 0), STATUS_SUCCESS);
    T_EQ(n, 64);
    T_TRUE(memcmp(buf, u"ChainingModeCBC", 32) == 0);

    T_CASE("ChainingMode with a 63-byte buffer");
    memset(buf, 0xA5, sizeof buf);
    n = 0;
    T_EQ(CALL(bcrypt, BCryptGetProperty, aes, u"ChainingMode", buf, 63, &n, 0), STATUS_BUFFER_TOO_SMALL);
    T_EQ(n, 64);
    T_TRUE(all_bytes(buf, sizeof buf, 0xA5));

    T_CASE("KeyStrength of a key");
    memset(secret, 0x3C, sizeof secret);
    memset(object, 0xA5, sizeof object);
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key, object, sizeof object, secret, 16, 0), STATUS_SUCCESS);
    T_EQ(CALL(bcrypt, BCryptGetProperty, (BCRYPT_HANDLE)key, u"KeyStrength", (PUCHAR)&strength, 4, &n, 0), STATUS_SUCCESS);
    T_EQ(strength, 128);
    T_EQ(n, 4);

    T_CASE("success and failure leave last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(bcrypt, BCryptGetProperty, aes, u"BlockLength", (PUCHAR)&blockLength, 4, &n, 0), STATUS_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(bcrypt, BCryptGetProperty, aes, u"ChainingMode", buf, 63, &n, 0), STATUS_BUFFER_TOO_SMALL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Core stops (RT-TRAPS, core:): the shim adds no guard; the core reports them. */
    T_TRAPS(CALL(bcrypt, BCryptGetProperty, aes, u"HashDigestLength", buf, 64, &n, 0), "rt_bcrypt_get_property: not implemented");
    T_TRAPS(CALL(bcrypt, BCryptGetProperty, aes, u"BlockLength", (PUCHAR)&blockLength, 4, &n, 1), "rt_bcrypt_get_property: not implemented");
}
