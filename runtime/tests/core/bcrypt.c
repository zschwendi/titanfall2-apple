/* rt/bcrypt.h (core/bcrypt.c): providers and symmetric keys as records.
 *
 * Expected values: the header's, each with its source there (Wine's bcrypt tests, Microsoft's
 * pages, the recording's arguments, or a choice of this runtime that the header marks).  The
 * sequence of the first case is the one the recording shows the engine making at 9.060 s.
 */
#include <string.h>

#include "rt/test.h"
#include "rt/bcrypt.h"

static bool all(const UCHAR *p, unsigned n, UCHAR v) {
    for (unsigned i = 0; i < n; i++) if (p[i] != v) return false;
    return true;
}

TEST(core, bcrypt)
{
    Ctx *ctx = T_CTX();
    BCRYPT_ALG_HANDLE aes = NULL, aes2 = NULL, rng = NULL;
    BCRYPT_KEY_HANDLE key = (BCRYPT_KEY_HANDLE)(uintptr_t)0x1111, key2 = NULL;
    UCHAR object[RT_BCRYPT_AES_OBJECT_LENGTH], secret[16], blob[0x230], out[80];
    ULONG n = 0, len = 0;
    rt_set_last_error(ctx, 0x5EA1);

    T_CASE("the engine's sequence: AES provider, mode by a string with cbInput 8, object length, RNG provider, 16 random bytes, key, opaque export, destroy");
    T_EQ(rt_bcrypt_open_provider(&aes, u"AES", NULL, 0), STATUS_SUCCESS);
    T_TRUE(RT_BCRYPT_IS_ALG(aes));
    T_EQ(rt_bcrypt_set_property(aes, u"ChainingMode", (PUCHAR)u"ChainingModeGCM", 8, 0), STATUS_SUCCESS);
    T_TRUE(T_LOGGED(RT_CH_NET, "cbInput 8 is not the string's 32 bytes"));
    T_EQ(rt_bcrypt_get_property(aes, u"ObjectLength", (PUCHAR)&len, 4, &n, 0), STATUS_SUCCESS);
    T_EQ(len, 654);
    T_EQ(n, 4);
    T_EQ(rt_bcrypt_open_provider(&rng, u"RNG", NULL, 0), STATUS_SUCCESS);
    T_TRUE(RT_BCRYPT_IS_ALG(rng) && rng != aes);
    memset(secret, 0, sizeof secret);
    T_EQ(rt_bcrypt_gen_random(rng, secret, 16, 0), STATUS_SUCCESS);
    T_TRUE(!all(secret, 16, 0));                              /* 2^-128 that a sound generator fails this */
    memset(object, 0xA5, sizeof object);
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key, object, sizeof object, secret, 16, 0), STATUS_SUCCESS);
    T_TRUE(RT_BCRYPT_IS_KEY(key));
    T_TRUE(all(object, sizeof object, 0xA5));                 /* the key object buffer is not written */
    memset(blob, 0xA5, sizeof blob);
    T_EQ(rt_bcrypt_export_key(key, NULL, u"OpaqueKeyBlob", blob, sizeof blob, &n, 0), STATUS_SUCCESS);
    T_EQ(n, 28);
    {
        RT_BCRYPT_OPAQUE_HEADER h;
        memcpy(&h, blob, sizeof h);
        T_EQ(h.magic, RT_BCRYPT_OPAQUE_MAGIC);
        T_EQ(h.version, 1);
        T_EQ(h.cbKeyData, 16);
        T_TRUE(memcmp(blob + 12, secret, 16) == 0);
        T_TRUE(all(blob + 28, sizeof blob - 28, 0xA5));
    }
    T_EQ(rt_bcrypt_destroy_key(key), STATUS_SUCCESS);
    T_EQ(rt_get_last_error(ctx), 0x5EA1);                     /* no function touches last error */

    T_CASE("each open gives a new handle; the implementation may be the primitive provider's name");
    T_EQ(rt_bcrypt_open_provider(&aes2, u"AES", u"Microsoft Primitive Provider", 0), STATUS_SUCCESS);
    T_TRUE(RT_BCRYPT_IS_ALG(aes2) && aes2 != aes && aes2 != rng);

    T_CASE("the chaining mode is CBC at first, is read back after a set, and belongs to one provider");
    memset(out, 0xA5, sizeof out);
    T_EQ(rt_bcrypt_get_property(aes2, u"ChainingMode", out, 64, &n, 0), STATUS_SUCCESS);
    T_EQ(n, 64);
    T_TRUE(memcmp(out, u"ChainingModeCBC", 32) == 0);
    T_TRUE(all(out + 32, sizeof out - 32, 0xA5));
    T_EQ(rt_bcrypt_get_property(aes, u"ChainingMode", out, 64, &n, 0), STATUS_SUCCESS);
    T_TRUE(memcmp(out, u"ChainingModeGCM", 32) == 0);
    T_EQ(rt_bcrypt_set_property(aes2, u"ChainingMode", (PUCHAR)u"ChainingModeCFB", 32, 0), STATUS_SUCCESS);
    T_EQ(rt_bcrypt_get_property(aes2, u"ChainingMode", out, 64, &n, 0), STATUS_SUCCESS);
    T_TRUE(memcmp(out, u"ChainingModeCFB", 32) == 0);
    T_EQ(rt_bcrypt_set_property(aes2, u"ChainingMode", (PUCHAR)u"ChainingModeCBC", 32, 0), STATUS_SUCCESS);

    T_CASE("a buffer under 64 bytes for the chaining mode: STATUS_BUFFER_TOO_SMALL, the size, nothing written");
    memset(out, 0xA5, sizeof out);
    n = 0;
    T_EQ(rt_bcrypt_get_property(aes, u"ChainingMode", out, 63, &n, 0), STATUS_BUFFER_TOO_SMALL);
    T_EQ(n, 64);
    T_TRUE(all(out, sizeof out, 0xA5));

    T_CASE("block length 16, key lengths 128 to 256 by 64");
    T_EQ(rt_bcrypt_get_property(aes, u"BlockLength", (PUCHAR)&len, 4, &n, 0), STATUS_SUCCESS);
    T_EQ(len, 16);
    T_EQ(n, 4);
    {
        BCRYPT_KEY_LENGTHS_STRUCT k;
        T_EQ(rt_bcrypt_get_property(aes, u"KeyLengths", (PUCHAR)&k, sizeof k, &n, 0), STATUS_SUCCESS);
        T_EQ(n, 12);
        T_EQ(k.dwMinLength, 128);
        T_EQ(k.dwMaxLength, 256);
        T_EQ(k.dwIncrement, 64);
    }

    T_CASE("a key: its strength in bits; a chaining mode on it is STATUS_NOT_SUPPORTED; the data blob");
    for (unsigned i = 0; i < 16; i++) secret[i] = (UCHAR)(0x10 + i);
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key2, object, sizeof object, secret, 16, 0), STATUS_SUCCESS);
    T_TRUE(key2 != key);                                      /* a destroyed key's value is not given out again at once */
    T_EQ(rt_bcrypt_get_property(key2, u"KeyStrength", (PUCHAR)&len, 4, &n, 0), STATUS_SUCCESS);
    T_EQ(len, 128);
    T_EQ(n, 4);
    T_EQ(rt_bcrypt_set_property(key2, u"ChainingMode", (PUCHAR)u"ChainingModeGCM", 32, 0), STATUS_NOT_SUPPORTED);
    memset(blob, 0xA5, sizeof blob);
    n = 0;
    T_EQ(rt_bcrypt_export_key(key2, NULL, u"KeyDataBlob", blob, 27, &n, 0), STATUS_BUFFER_TOO_SMALL);
    T_EQ(n, 28);
    T_TRUE(all(blob, sizeof blob, 0xA5));
    T_EQ(rt_bcrypt_export_key(key2, NULL, u"KeyDataBlob", blob, 28, &n, 0), STATUS_SUCCESS);
    T_EQ(n, 28);
    {
        BCRYPT_KEY_DATA_BLOB_HEADER h;
        memcpy(&h, blob, sizeof h);
        T_EQ(h.dwMagic, 0x4d42444b);
        T_EQ(h.dwVersion, 1);
        T_EQ(h.cbKeyData, 16);
        T_TRUE(memcmp(blob + 12, secret, 16) == 0);
        T_TRUE(all(blob + 28, sizeof blob - 28, 0xA5));
    }
    n = 0;
    T_EQ(rt_bcrypt_export_key(key2, NULL, u"OpaqueKeyBlob", blob, 27, &n, 0), STATUS_BUFFER_TOO_SMALL);
    T_EQ(n, 28);

    T_CASE("keys of 24 and 32 bytes");
    {
        BCRYPT_KEY_HANDLE k24 = NULL, k32 = NULL;
        UCHAR big[32];
        memset(big, 0x42, sizeof big);
        T_EQ(rt_bcrypt_generate_symmetric_key(aes, &k24, object, sizeof object, big, 24, 0), STATUS_SUCCESS);
        T_EQ(rt_bcrypt_generate_symmetric_key(aes, &k32, object, sizeof object, big, 32, 0), STATUS_SUCCESS);
        T_EQ(rt_bcrypt_get_property(k24, u"KeyStrength", (PUCHAR)&len, 4, &n, 0), STATUS_SUCCESS);
        T_EQ(len, 192);
        T_EQ(rt_bcrypt_export_key(k32, NULL, u"KeyDataBlob", blob, sizeof blob, &n, 0), STATUS_SUCCESS);
        T_EQ(n, 44);
        T_EQ(rt_bcrypt_destroy_key(k24), STATUS_SUCCESS);
        T_EQ(rt_bcrypt_destroy_key(k32), STATUS_SUCCESS);
    }

    T_CASE("the failures of a key's making leave *phKey alone");
    key = (BCRYPT_KEY_HANDLE)(uintptr_t)0x1111;
    T_EQ(rt_bcrypt_generate_symmetric_key(NULL, &key, object, sizeof object, secret, 16, 0), STATUS_INVALID_HANDLE);
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key, object, sizeof object, secret, 1, 0), STATUS_INVALID_PARAMETER);
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key, object, sizeof object, secret, 17, 0), STATUS_INVALID_PARAMETER);
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key, object, 653, secret, 16, 0), STATUS_BUFFER_TOO_SMALL);
    T_TRUE(key == (BCRYPT_KEY_HANDLE)(uintptr_t)0x1111);

    T_CASE("destroying NULL is STATUS_INVALID_HANDLE; last error is never touched");
    T_EQ(rt_bcrypt_destroy_key(NULL), STATUS_INVALID_HANDLE);
    T_EQ(rt_bcrypt_destroy_key(key2), STATUS_SUCCESS);
    T_EQ(rt_get_last_error(ctx), 0x5EA1);

    /* the stops, each naming what was asked */
    T_TRAPS(rt_bcrypt_destroy_key(key2), "rt_bcrypt_destroy_key: not implemented: handle");
    T_TRAPS(rt_bcrypt_open_provider(&aes2, u"SHA256", NULL, 0), "rt_bcrypt_open_provider: not implemented: algorithm \"SHA256\"");
    T_TRAPS(rt_bcrypt_open_provider(&aes2, u"AES", u"Other", 0), "rt_bcrypt_open_provider: not implemented: implementation \"Other\"");
    T_TRAPS(rt_bcrypt_open_provider(&aes2, u"AES", NULL, 8), "rt_bcrypt_open_provider: not implemented: flags 0x8");
    T_TRAPS(rt_bcrypt_open_provider(NULL, u"AES", NULL, 0), "rt_bcrypt_open_provider: not implemented: a NULL handle pointer");
    T_TRAPS(rt_bcrypt_gen_random(aes, secret, 16, 0), "rt_bcrypt_gen_random: not implemented: handle");
    T_TRAPS(rt_bcrypt_gen_random(rng, NULL, 16, 0), "rt_bcrypt_gen_random: not implemented: a NULL buffer");
    T_TRAPS(rt_bcrypt_gen_random(rng, secret, 16, 1), "rt_bcrypt_gen_random: not implemented: flags 0x1");
    T_TRAPS(rt_bcrypt_get_property(aes, u"HashDigestLength", out, 4, &n, 0), "rt_bcrypt_get_property: not implemented: property \"HashDigestLength\" of the AES provider");
    T_TRAPS(rt_bcrypt_get_property(rng, u"ObjectLength", out, 4, &n, 0), "rt_bcrypt_get_property: not implemented: property \"ObjectLength\" of the RNG provider");
    T_TRAPS(rt_bcrypt_get_property(aes, u"ObjectLength", out, 3, &n, 0), "rt_bcrypt_get_property: not implemented: ObjectLength with a buffer of 3 bytes");
    T_TRAPS(rt_bcrypt_get_property(aes, u"ObjectLength", NULL, 0, &n, 0), "rt_bcrypt_get_property: not implemented: a NULL buffer");
    T_TRAPS(rt_bcrypt_set_property(aes, u"ChainingMode", (PUCHAR)u"ChainingModeECB", 32, 0), "rt_bcrypt_set_property: not implemented: chaining mode \"ChainingModeECB\"");
    T_TRAPS(rt_bcrypt_set_property(aes, u"KeyLength", (PUCHAR)&len, 4, 0), "rt_bcrypt_set_property: not implemented: property \"KeyLength\"");
    T_TRAPS(rt_bcrypt_generate_symmetric_key(rng, &key, object, sizeof object, secret, 16, 0), "rt_bcrypt_generate_symmetric_key: not implemented: handle");
    T_TRAPS(rt_bcrypt_generate_symmetric_key(aes, &key, object, sizeof object, secret, 20, 0), "rt_bcrypt_generate_symmetric_key: not implemented: a secret of 20 bytes");
    T_TRAPS(rt_bcrypt_generate_symmetric_key(aes, &key, NULL, 0, secret, 16, 0), "rt_bcrypt_generate_symmetric_key: not implemented: no key object buffer");
    T_TRAPS(rt_bcrypt_export_key(key2, NULL, u"KeyDataBlob", blob, sizeof blob, &n, 0), "rt_bcrypt_export_key: not implemented: handle");
    T_EQ(rt_bcrypt_generate_symmetric_key(aes, &key2, object, sizeof object, secret, 16, 0), STATUS_SUCCESS);
    T_TRAPS(rt_bcrypt_export_key(key2, NULL, u"AesWrapKeyBlob", blob, sizeof blob, &n, 0), "rt_bcrypt_export_key: not implemented: blob type \"AesWrapKeyBlob\"");
    T_TRAPS(rt_bcrypt_export_key(key2, key2, u"KeyDataBlob", blob, sizeof blob, &n, 0), "rt_bcrypt_export_key: not implemented: an export key");
    T_TRAPS(rt_bcrypt_export_key(key2, NULL, u"KeyDataBlob", NULL, 0, &n, 0), "rt_bcrypt_export_key: not implemented: a NULL buffer");
    T_EQ(rt_bcrypt_destroy_key(key2), STATUS_SUCCESS);
}
