/* bcrypt: algorithm providers and symmetric keys as records.  Interface and specification:
 * rt/bcrypt.h (every case, its source and every stop is there; nothing is decided here).
 * Built by unit rt-census-rows.  No cipher is computed: a provider is a name and a chaining
 * mode, a key is a copy of its secret.  One mutex guards both tables; it is never held across
 * rt_fatal's caller-visible work except in the stop itself, which does not return.
 */
#include <pthread.h>
#include <string.h>

#include "rt/bcrypt.h"
#include "rt/log.h"
#include "rt/machine.h"

#define STOP(fn, fmt, ...)  rt_fatal(fn ": not implemented: " fmt, ##__VA_ARGS__)

enum { ALG_FREE = 0, ALG_AES, ALG_RNG };
enum { MODE_CBC = 0, MODE_CFB, MODE_GCM, MODE_COUNT };
enum { MAX_ALGS = 32, MAX_KEYS = 64, MODE_CHARS = 16 /* "ChainingModeXXX" and its terminator */ };

static const WCHAR *const MODE_NAME[MODE_COUNT] = { BCRYPT_CHAIN_MODE_CBC, BCRYPT_CHAIN_MODE_CFB, BCRYPT_CHAIN_MODE_GCM };

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct { int kind, mode; } algs[MAX_ALGS];
static struct { int live; unsigned gen; ULONG len; UCHAR secret[32]; } keys[MAX_KEYS];

static bool weq(const WCHAR *a, const WCHAR *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* A few characters of a wide string for a stop's text (ASCII only, the rest as '?'). */
static const char *narrow(const WCHAR *w, char buf[48]) {
    unsigned i = 0;
    for (; w && w[i] && i < 47; i++) buf[i] = (w[i] >= 0x20 && w[i] < 0x7f) ? (char)w[i] : '?';
    buf[i] = 0;
    return w ? buf : "(null)";
}

static BCRYPT_ALG_HANDLE alg_handle(unsigned i) { return (BCRYPT_ALG_HANDLE)(uintptr_t)(0x0BC10000u | i); }
static BCRYPT_KEY_HANDLE key_handle(unsigned i) {
    return (BCRYPT_KEY_HANDLE)(uintptr_t)(0x4B000000u | ((keys[i].gen & 0xFFFFu) << 8) | i);
}

/* The provider kind behind a handle, ALG_FREE if it is none; *index receives the slot. */
static int alg_kind(BCRYPT_HANDLE h, unsigned *index) {
    unsigned i = (unsigned)((uintptr_t)h & 0xFFFFu);
    if (!RT_BCRYPT_IS_ALG(h) || i >= MAX_ALGS) return ALG_FREE;
    *index = i;
    return algs[i].kind;
}

/* The slot of a live key, or -1. */
static int key_slot(BCRYPT_HANDLE h) {
    unsigned i = (unsigned)((uintptr_t)h & 0xFFu);
    if (!RT_BCRYPT_IS_KEY(h) || i >= MAX_KEYS || !keys[i].live || key_handle(i) != h) return -1;
    return (int)i;
}

NTSTATUS rt_bcrypt_open_provider(BCRYPT_ALG_HANDLE *out, LPCWSTR alg, LPCWSTR impl, ULONG flags) {
    char text[48];
    if (!out || !alg) STOP("rt_bcrypt_open_provider", "a NULL %s", out ? "algorithm name" : "handle pointer");
    int kind = weq(alg, BCRYPT_AES_ALGORITHM) ? ALG_AES : weq(alg, BCRYPT_RNG_ALGORITHM) ? ALG_RNG : ALG_FREE;
    if (kind == ALG_FREE) STOP("rt_bcrypt_open_provider", "algorithm \"%s\"", narrow(alg, text));
    if (impl && !weq(impl, MS_PRIMITIVE_PROVIDER))
        STOP("rt_bcrypt_open_provider", "implementation \"%s\" for \"%s\"", narrow(impl, text), kind == ALG_AES ? "AES" : "RNG");
    if (flags) STOP("rt_bcrypt_open_provider", "flags 0x%x", (unsigned)flags);
    pthread_mutex_lock(&lock);
    unsigned i = 0;
    while (i < MAX_ALGS && algs[i].kind != ALG_FREE) i++;
    if (i == MAX_ALGS) STOP("rt_bcrypt_open_provider", "more than %d providers (none is ever closed)", MAX_ALGS);
    algs[i].kind = kind;
    algs[i].mode = MODE_CBC;
    pthread_mutex_unlock(&lock);
    *out = alg_handle(i);
    return STATUS_SUCCESS;
}

NTSTATUS rt_bcrypt_gen_random(BCRYPT_ALG_HANDLE alg, PUCHAR buf, ULONG cb, ULONG flags) {
    unsigned i = 0;
    if (flags) STOP("rt_bcrypt_gen_random", "flags 0x%x with a provider handle", (unsigned)flags);
    pthread_mutex_lock(&lock);
    int kind = alg_kind(alg, &i);
    pthread_mutex_unlock(&lock);
    if (kind != ALG_RNG)
        STOP("rt_bcrypt_gen_random", "handle %p is %s", alg, kind == ALG_AES ? "an AES provider" : "no provider");
    if (!buf) STOP("rt_bcrypt_gen_random", "a NULL buffer (%u bytes)", (unsigned)cb);
    rt_random_bytes(buf, cb);
    return STATUS_SUCCESS;
}

static NTSTATUS put_ulong(const char *prop, PUCHAR out, ULONG cb, ULONG *result, ULONG value) {
    if (cb < sizeof value) STOP("rt_bcrypt_get_property", "%s with a buffer of %u bytes", prop, (unsigned)cb);
    memcpy(out, &value, sizeof value);
    *result = sizeof value;
    return STATUS_SUCCESS;
}

NTSTATUS rt_bcrypt_get_property(BCRYPT_HANDLE h, LPCWSTR prop, PUCHAR out, ULONG cb, ULONG *result, ULONG flags) {
    char text[48];
    unsigned i = 0;
    if (!prop || !out || !result)
        STOP("rt_bcrypt_get_property", "a NULL %s", !prop ? "property name" : !out ? "buffer" : "result pointer");
    if (flags) STOP("rt_bcrypt_get_property", "flags 0x%x", (unsigned)flags);
    pthread_mutex_lock(&lock);
    int kind = alg_kind(h, &i), mode = kind == ALG_AES ? algs[i].mode : 0, k = key_slot(h);
    ULONG bits = k >= 0 ? keys[k].len * 8 : 0;
    pthread_mutex_unlock(&lock);
    if (kind == ALG_AES) {
        if (weq(prop, BCRYPT_OBJECT_LENGTH)) return put_ulong("ObjectLength", out, cb, result, RT_BCRYPT_AES_OBJECT_LENGTH);
        if (weq(prop, BCRYPT_BLOCK_LENGTH)) return put_ulong("BlockLength", out, cb, result, 16);
        if (weq(prop, BCRYPT_KEY_LENGTHS)) {
            const BCRYPT_KEY_LENGTHS_STRUCT lengths = { 128, 256, 64 };
            if (cb < sizeof lengths) STOP("rt_bcrypt_get_property", "KeyLengths with a buffer of %u bytes", (unsigned)cb);
            memcpy(out, &lengths, sizeof lengths);
            *result = sizeof lengths;
            return STATUS_SUCCESS;
        }
        if (weq(prop, BCRYPT_CHAINING_MODE)) {
            *result = 64;
            if (cb < 64) return STATUS_BUFFER_TOO_SMALL;
            memcpy(out, MODE_NAME[mode], MODE_CHARS * sizeof(WCHAR));
            return STATUS_SUCCESS;
        }
    } else if (k >= 0 && weq(prop, BCRYPT_KEY_STRENGTH)) {
        return put_ulong("KeyStrength", out, cb, result, bits);
    }
    STOP("rt_bcrypt_get_property", "property \"%s\" of %s %p", narrow(prop, text),
         kind == ALG_AES ? "the AES provider" : kind == ALG_RNG ? "the RNG provider" : k >= 0 ? "the key" : "the unknown object", h);
}

NTSTATUS rt_bcrypt_set_property(BCRYPT_HANDLE h, LPCWSTR prop, PUCHAR in, ULONG cb, ULONG flags) {
    char text[48];
    unsigned i = 0;
    if (!prop || !in) STOP("rt_bcrypt_set_property", "a NULL %s", !prop ? "property name" : "value");
    if (flags) STOP("rt_bcrypt_set_property", "flags 0x%x", (unsigned)flags);
    pthread_mutex_lock(&lock);
    int kind = alg_kind(h, &i), k = key_slot(h);
    pthread_mutex_unlock(&lock);
    if (!weq(prop, BCRYPT_CHAINING_MODE) || (kind != ALG_AES && k < 0))
        STOP("rt_bcrypt_set_property", "property \"%s\" of %s %p", narrow(prop, text),
             kind == ALG_AES ? "the AES provider" : kind == ALG_RNG ? "the RNG provider" : k >= 0 ? "the key" : "the unknown object", h);
    if (k >= 0) return STATUS_NOT_SUPPORTED;                 /* Windows 7: no chaining mode on a key */
    /* The value is a mode's name.  It is read to its terminator, at most 16 characters: every
     * name this file knows has 15, so the 16th must be the terminator. */
    const WCHAR *value = (const WCHAR *)in;
    int mode = 0;
    while (mode < MODE_COUNT && !weq(value, MODE_NAME[mode])) mode++;
    if (mode == MODE_COUNT) {
        WCHAR shown[MODE_CHARS + 1] = { 0 };
        for (unsigned n = 0; n < MODE_CHARS && value[n]; n++) shown[n] = value[n];
        STOP("rt_bcrypt_set_property", "chaining mode \"%s\"", narrow(shown, text));
    }
    if (cb != MODE_CHARS * sizeof(WCHAR))
        rt_log_once(RT_CH_NET, RT_LL_WARN, "bcrypt.dll!BCryptSetProperty",
                    "BCryptSetProperty(ChainingMode): cbInput %u is not the string's 32 bytes; the string is read to its terminator",
                    (unsigned)cb);
    pthread_mutex_lock(&lock);
    algs[i].mode = mode;
    pthread_mutex_unlock(&lock);
    return STATUS_SUCCESS;
}

NTSTATUS rt_bcrypt_generate_symmetric_key(BCRYPT_ALG_HANDLE alg, BCRYPT_KEY_HANDLE *key, PUCHAR object,
                                          ULONG cb_object, PUCHAR secret, ULONG cb_secret, ULONG flags) {
    unsigned i = 0;
    if (!alg) return STATUS_INVALID_HANDLE;
    if (cb_secret == 1 || cb_secret == 17) return STATUS_INVALID_PARAMETER;
    if (object && cb_object < RT_BCRYPT_AES_OBJECT_LENGTH) return STATUS_BUFFER_TOO_SMALL;
    if (!key || !secret) STOP("rt_bcrypt_generate_symmetric_key", "a NULL %s", !key ? "key pointer" : "secret");
    if (!object) STOP("rt_bcrypt_generate_symmetric_key", "no key object buffer (cbKeyObject %u)", (unsigned)cb_object);
    if (flags) STOP("rt_bcrypt_generate_symmetric_key", "flags 0x%x", (unsigned)flags);
    if (cb_secret != 16 && cb_secret != 24 && cb_secret != 32)
        STOP("rt_bcrypt_generate_symmetric_key", "a secret of %u bytes", (unsigned)cb_secret);
    pthread_mutex_lock(&lock);
    int kind = alg_kind(alg, &i);
    if (kind != ALG_AES)
        STOP("rt_bcrypt_generate_symmetric_key", "handle %p is %s", alg, kind == ALG_RNG ? "the RNG provider" : "no provider");
    unsigned k = 0;
    while (k < MAX_KEYS && keys[k].live) k++;
    if (k == MAX_KEYS) STOP("rt_bcrypt_generate_symmetric_key", "more than %d keys alive", MAX_KEYS);
    keys[k].live = 1;
    keys[k].gen++;
    keys[k].len = cb_secret;
    memcpy(keys[k].secret, secret, cb_secret);
    BCRYPT_KEY_HANDLE handle = key_handle(k);
    pthread_mutex_unlock(&lock);
    *key = handle;
    return STATUS_SUCCESS;
}

NTSTATUS rt_bcrypt_export_key(BCRYPT_KEY_HANDLE key, BCRYPT_KEY_HANDLE export_key, LPCWSTR type, PUCHAR out,
                              ULONG cb, ULONG *result, ULONG flags) {
    char text[48];
    if (!type || !out || !result)
        STOP("rt_bcrypt_export_key", "a NULL %s", !type ? "blob type" : !out ? "buffer" : "result pointer");
    if (export_key) STOP("rt_bcrypt_export_key", "an export key (%p)", export_key);
    if (flags) STOP("rt_bcrypt_export_key", "flags 0x%x", (unsigned)flags);
    bool data = weq(type, BCRYPT_KEY_DATA_BLOB);
    if (!data && !weq(type, BCRYPT_OPAQUE_KEY_BLOB)) STOP("rt_bcrypt_export_key", "blob type \"%s\"", narrow(type, text));
    pthread_mutex_lock(&lock);
    int k = key_slot(key);
    if (k < 0) STOP("rt_bcrypt_export_key", "handle %p is no live key", key);
    ULONG len = keys[k].len;
    UCHAR secret[32];
    memcpy(secret, keys[k].secret, len);
    pthread_mutex_unlock(&lock);
    *result = 12 + len;
    if (cb < 12 + len) return STATUS_BUFFER_TOO_SMALL;
    const ULONG header[3] = { data ? BCRYPT_KEY_DATA_BLOB_MAGIC : RT_BCRYPT_OPAQUE_MAGIC, 1, len };
    memcpy(out, header, 12);
    memcpy(out + 12, secret, len);
    return STATUS_SUCCESS;
}

NTSTATUS rt_bcrypt_destroy_key(BCRYPT_KEY_HANDLE key) {
    if (!key) return STATUS_INVALID_HANDLE;
    pthread_mutex_lock(&lock);
    int k = key_slot(key);
    if (k < 0) STOP("rt_bcrypt_destroy_key", "handle %p is no live key", key);
    keys[k].live = 0;
    keys[k].len = 0;
    memset(keys[k].secret, 0, sizeof keys[k].secret);
    pthread_mutex_unlock(&lock);
    return STATUS_SUCCESS;
}
