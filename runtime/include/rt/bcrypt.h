/* bcrypt: algorithm providers and symmetric keys as records (core/bcrypt.c, built; unit
 * rt-census-rows).  The specification of the shims of bcrypt.dll that the recorded start
 * calls: on the network thread at 9.060 s the engine opens five AES providers and one RNG
 * provider, and for each AES provider sets the chaining mode, asks the object length, draws
 * 16 random bytes, makes a key of them and exports it (docs/reviews/census-rows.md 1.6;
 * docs/handoff/rt-census-rows.md has the calls with their arguments).  No BCryptEncrypt,
 * BCryptDecrypt, BCryptImportKey or BCryptCloseAlgorithmProvider is called in the whole
 * recording, so NO CIPHER IS COMPUTED HERE: a provider is a name and a mode, a key is its
 * secret.  The four functions named stay stops; the day one is reached, the cipher is the
 * host's CommonCrypto (docs/handoff/rt-shim-rows-2.md, bcrypt).
 *
 * SOURCES.  [W] Wine's conformance test dlls/bcrypt/tests/bcrypt.c (wine-mirror master), the
 * function named; [MS] the function's page on learn.microsoft.com; [T] the recording,
 * `tools/trace/cut.py mission1 calls --commit-safe --from engine --dll bcrypt --t 9.05:9.07`;
 * CHOICE: this runtime's choice, with the reason.  Each [W] and [MS] statement is as
 * docs/reviews/census-rows.md 1.6 read it on 2026-10-11.
 *
 * HANDLES.  Not kernel handles: CloseHandle does not know them.  A provider handle and a key
 * handle are values of this file's own (RT_BCRYPT_IS_ALG, RT_BCRYPT_IS_KEY say which a value
 * looks like); a destroyed key's value is not given out again until 65,536 keys later.
 * LAST ERROR is never touched: every function returns an NTSTATUS ([MS] "Returns a status
 * code").  STOPS below is the unimplemented report (rt_fatal, "<function>: not implemented:
 * ..."), which names the value.  Every function may be called on any thread.
 */
#ifndef RT_BCRYPT_H
#define RT_BCRYPT_H

#include "rt/wintypes.h"
#include "rt/win/error.h"
#include "rt/win/file.h"            /* STATUS_SUCCESS */
#include "rt/win/devices.h"

#define RT_BCRYPT_IS_ALG(h)  (((uintptr_t)(h) & 0xFFFF0000u) == 0x0BC10000u && ((uintptr_t)(h) >> 32) == 0)
#define RT_BCRYPT_IS_KEY(h)  (((uintptr_t)(h) & 0xFF000000u) == 0x4B000000u && ((uintptr_t)(h) >> 32) == 0)

/* What BCRYPT_OBJECT_LENGTH answers for AES: 654.  CHOICE: the number is the provider's own
 * and no page gives it; 654 is what the recording's machine answered ([T]: the game passes
 * cbKeyObject 0x28e in the next call), so the game allocates here what it allocated there. */
#define RT_BCRYPT_AES_OBJECT_LENGTH  654u

/* The opaque key blob of this runtime: this header, then the secret.  CHOICE: [MS] calls the
 * blob opaque, "specific to a single CSP" and not transferable, so its layout is the
 * provider's own and only this provider's import (not built) will ever read it. */
#define RT_BCRYPT_OPAQUE_MAGIC  0x4B4F5452u      /* "RTOK" */
typedef struct { ULONG magic, version, cbKeyData; } RT_BCRYPT_OPAQUE_HEADER;

/* BCryptOpenAlgorithmProvider(phAlgorithm, pszAlgId, pszImplementation, dwFlags).
 *   L"AES" with implementation NULL or L"Microsoft Primitive Provider" and flags 0:
 *       STATUS_SUCCESS, *out a new provider whose chaining mode is CBC ([W] test_aes,
 *       test_BCryptGenerateSymmetricKey; the default mode: [W] test_aes reads it back).
 *   L"RNG" with the same implementation and flags: STATUS_SUCCESS, *out a new provider
 *       ([T]: the game's call at engine+0x215472 returned 0; [MS] BCryptGenRandom: the handle
 *       is "created by using the BCryptOpenAlgorithmProvider function").
 * Each call gives a new handle (the recording shows five different ones for L"AES").
 * STOPS: out or alg NULL; any other algorithm (Windows has many, and STATUS_NOT_FOUND would
 * be a lie for a real one), any other implementation, any flag; a 33rd provider (none is ever
 * closed: BCryptCloseAlgorithmProvider is not built). */
NTSTATUS rt_bcrypt_open_provider(BCRYPT_ALG_HANDLE *out, LPCWSTR alg, LPCWSTR impl, ULONG flags);

/* BCryptGenRandom(hAlgorithm, pbBuffer, cbBuffer, dwFlags) for a handle that is not NULL and
 * flags 0 (the shim keeps the NULL handle with BCRYPT_USE_SYSTEM_PREFERRED_RNG to itself).
 *   an RNG provider: cb bytes of rt_random_bytes, STATUS_SUCCESS ([T]: the game's five calls,
 *       16 bytes each, flags 0; [MS] as above).  cb 0 writes nothing and succeeds (CHOICE: as
 *       the shim answers it for the system generator, [W] test_BCryptGenRandom).
 * STOPS: buf NULL; an AES provider ([MS]: the algorithm "must support the random number
 * generator interface"; the status Windows gives is not on the page); a value that is no
 * provider of this file; any flag. */
NTSTATUS rt_bcrypt_gen_random(BCRYPT_ALG_HANDLE alg, PUCHAR buf, ULONG cb, ULONG flags);

/* BCryptGetProperty(hObject, pszProperty, pbOutput, cbOutput, pcbResult, dwFlags).
 * On an AES provider ([W] test_aes):
 *   L"ObjectLength"  a ULONG, RT_BCRYPT_AES_OBJECT_LENGTH; *result 4.
 *   L"BlockLength"   a ULONG, 16; *result 4.
 *   L"KeyLengths"    a BCRYPT_KEY_LENGTHS_STRUCT {128, 256, 64}; *result 12.
 *   L"ChainingMode"  the mode's name with its terminator (32 bytes) and *result 64; with cb
 *                    under 64 STATUS_BUFFER_TOO_SMALL, *result 64 and nothing written.  (The
 *                    other 32 bytes of a large enough buffer are left alone: CHOICE, what
 *                    Windows puts there was not read.)
 * On a key ([W] test_BCryptGenerateSymmetricKey):
 *   L"KeyStrength"   a ULONG, the secret's bits (128 for 16 bytes); *result 4.
 * STOPS: prop, out or result NULL; cb too small for one of the fixed-size answers (the size
 * query and the short buffer were not read for them); any other property or object (the RNG
 * provider among them); any flag. */
NTSTATUS rt_bcrypt_get_property(BCRYPT_HANDLE h, LPCWSTR prop, PUCHAR out, ULONG cb, ULONG *result, ULONG flags);

/* BCryptSetProperty(hObject, pszProperty, pbInput, cbInput, dwFlags).
 *   an AES provider, L"ChainingMode", the string L"ChainingModeCBC", L"ChainingModeCFB" or
 *       L"ChainingModeGCM": STATUS_SUCCESS, and the getter reads it back ([W] test_aes).
 *       cb IS NOT LOOKED AT: the game passes 8 (the size of a pointer) for a string of 32
 *       bytes at engine+0x2155fd, and the recording's machine answered 0 ([T]).  CHOICE: the
 *       string is read to its terminator; what Windows does with a count shorter than the
 *       string is not on the page.  One WARN line per process (channel net: these are
 *       the network channel's keys) says so when cb is not 32.
 *   a key, L"ChainingMode": STATUS_NOT_SUPPORTED, nothing changes (Windows 7, the machine of
 *       docs/RUNTIME.md 8.5: [W] test_BCryptGenerateSymmetricKey accepts it as the result
 *       before Windows 8).
 * STOPS: prop or in NULL; another mode (ECB, CCM and the rest: not read, the stop names it);
 * any other property or object; any flag. */
NTSTATUS rt_bcrypt_set_property(BCRYPT_HANDLE h, LPCWSTR prop, PUCHAR in, ULONG cb, ULONG flags);

/* BCryptGenerateSymmetricKey(hAlgorithm, phKey, pbKeyObject, cbKeyObject, pbSecret, cbSecret,
 * dwFlags).  In this order:
 *   alg NULL: STATUS_INVALID_HANDLE, *key untouched ([W]).
 *   cb_secret 1 or 17: STATUS_INVALID_PARAMETER, *key untouched ([W]).
 *   object not NULL and cb_object under RT_BCRYPT_AES_OBJECT_LENGTH: STATUS_BUFFER_TOO_SMALL,
 *       *key untouched ([MS]).
 *   cb_secret 16, 24 or 32 on an AES provider: STATUS_SUCCESS and *key a new key holding a
 *       copy of the secret ([W]; [MS]).  The key object buffer is NOT written (CHOICE: what
 *       Windows keeps there is the provider's own; the record is this file's).
 * STOPS: key or secret NULL; object NULL (Windows 7 allocates the object itself then: from
 * memory of the page, not read); any other secret length (only 1 and 17 are asserted); a
 * provider that is not AES, or a value that is no provider; any flag; a 65th live key. */
NTSTATUS rt_bcrypt_generate_symmetric_key(BCRYPT_ALG_HANDLE alg, BCRYPT_KEY_HANDLE *key, PUCHAR object,
                                          ULONG cb_object, PUCHAR secret, ULONG cb_secret, ULONG flags);

/* BCryptExportKey(hKey, hExportKey, pszBlobType, pbOutput, cbOutput, pcbResult, dwFlags) with
 * no export key.  The size is 12 + the secret's bytes for both blob types.
 *   L"KeyDataBlob":   cb under the size: STATUS_BUFFER_TOO_SMALL and *result the size, nothing
 *       written; otherwise STATUS_SUCCESS, *result the size, and a BCRYPT_KEY_DATA_BLOB_HEADER
 *       (magic, version 1, the length) followed by the secret ([W] test_key_import_export).
 *   L"OpaqueKeyBlob": the same with an RT_BCRYPT_OPAQUE_HEADER (CHOICE, above; the game's six
 *       calls ask for this type with a buffer of 0x230 bytes: [T]).
 * STOPS: type, out or result NULL (the size query with no buffer was not read); an export
 * key; any other blob type; a value that is no live key; any flag. */
NTSTATUS rt_bcrypt_export_key(BCRYPT_KEY_HANDLE key, BCRYPT_KEY_HANDLE export_key, LPCWSTR type, PUCHAR out,
                              ULONG cb, ULONG *result, ULONG flags);

/* BCryptDestroyKey(hKey): STATUS_SUCCESS for a live key, whose record is dropped and wiped;
 * STATUS_INVALID_HANDLE for NULL ([W] test_BCryptDecrypt, at its end).
 * STOPS: any other value, a key destroyed before among them ([W] has that call disabled). */
NTSTATUS rt_bcrypt_destroy_key(BCRYPT_KEY_HANDLE key);

#endif /* RT_BCRYPT_H */
