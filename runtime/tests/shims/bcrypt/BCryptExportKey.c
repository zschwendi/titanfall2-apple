#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/bcrypt.h"

/* The key is the 16 bytes 0x10 to 0x1F.  The buffers are preset to 0xA5 so a write shows. */
#define SENT  0xA5

static bool all(const UCHAR *p, unsigned n, UCHAR v)
{
    for (unsigned i = 0; i < n; i++) if (p[i] != v) return false;
    return true;
}

TEST(bcrypt, BCryptExportKey)
{
    BCRYPT_ALG_HANDLE aes = NULL;
    UCHAR object[RT_BCRYPT_AES_OBJECT_LENGTH], secret[16], buf[0x230];
    BCRYPT_KEY_HANDLE key = NULL;
    ULONG n = 0;

    for (unsigned i = 0; i < 16; i++) secret[i] = (UCHAR)(0x10 + i);
    T_EQ(rt_bcrypt_open_provider(&aes, u"AES", NULL, 0), STATUS_SUCCESS);
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key, object, sizeof object, secret, 16, 0), STATUS_SUCCESS);

    T_CASE("exports a key as an opaque key blob into the game's buffer, leaving the rest of it");
    CALL(kernel32, SetLastError, 0x1234);
    memset(buf, SENT, sizeof buf);
    n = 0;
    T_EQ(CALL(bcrypt, BCryptExportKey, key, NULL, u"OpaqueKeyBlob", buf, sizeof buf, &n, 0), STATUS_SUCCESS);
    T_EQ(n, 28);
    {
        RT_BCRYPT_OPAQUE_HEADER h;
        memcpy(&h, buf, sizeof h);
        T_EQ(h.magic, RT_BCRYPT_OPAQUE_MAGIC);
        T_EQ(h.version, 1);
        T_EQ(h.cbKeyData, 16);
    }
    T_TRUE(memcmp(buf + 12, secret, 16) == 0);
    T_TRUE(all(buf + 28, sizeof buf - 28, SENT));
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("exports a key as a key data blob");
    memset(buf, SENT, sizeof buf);
    n = 0;
    T_EQ(CALL(bcrypt, BCryptExportKey, key, NULL, u"KeyDataBlob", buf, 28, &n, 0), STATUS_SUCCESS);
    T_EQ(n, 28);
    {
        ULONG w[3];
        memcpy(w, buf, sizeof w);
        T_EQ(w[0], 0x4d42444b);
        T_EQ(w[1], 1);
        T_EQ(w[2], 16);
    }
    T_TRUE(memcmp(buf + 12, secret, 16) == 0);

    T_CASE("a buffer under the size is STATUS_BUFFER_TOO_SMALL with the size stored and nothing written");
    memset(buf, SENT, sizeof buf);
    n = 0;
    T_EQ(CALL(bcrypt, BCryptExportKey, key, NULL, u"OpaqueKeyBlob", buf, 27, &n, 0), STATUS_BUFFER_TOO_SMALL);
    T_EQ(n, 28);
    T_TRUE(all(buf, sizeof buf, SENT));
    n = 0;
    T_EQ(CALL(bcrypt, BCryptExportKey, key, NULL, u"KeyDataBlob", buf, 27, &n, 0), STATUS_BUFFER_TOO_SMALL);
    T_EQ(n, 28);
    T_TRUE(all(buf, sizeof buf, SENT));
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("success and failure leave last error alone");
    memset(buf, SENT, sizeof buf);
    T_EQ(CALL(bcrypt, BCryptExportKey, key, NULL, u"KeyDataBlob", buf, sizeof buf, &n, 0), STATUS_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(bcrypt, BCryptExportKey, key, NULL, u"KeyDataBlob", buf, 27, &n, 0), STATUS_BUFFER_TOO_SMALL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(bcrypt, BCryptExportKey, key, NULL, u"AesWrapKeyBlob", buf, sizeof buf, &n, 0), "rt_bcrypt_export_key: not implemented");
    T_TRAPS(CALL(bcrypt, BCryptExportKey, key, key, u"OpaqueKeyBlob", buf, sizeof buf, &n, 0), "rt_bcrypt_export_key: not implemented");
    T_TRAPS(CALL(bcrypt, BCryptExportKey, key, NULL, u"OpaqueKeyBlob", NULL, 0, &n, 0), "rt_bcrypt_export_key: not implemented");
}
