/* setupapi, hid and bcrypt: the types the shims of the device-enumeration and cryptography
 * functions name (setupapi.h, hidsdi.h, bcrypt.h).  The machine this runtime describes has
 * no HID device (docs/RUNTIME.md section 9.2 names that as an `absent` answer), so the only
 * structure declared is the one a caller hands in to be filled. */
#ifndef RT_WIN_DEVICES_H
#define RT_WIN_DEVICES_H

#include <stddef.h>
#include "rt/wintypes.h"
#include "rt/win/com.h"

/* A device information set (SetupDiGetClassDevs). */
typedef void *HDEVINFO;

/* The one device information set this runtime has: the empty one (no HID device exists).  A
 * value of our own, not a kernel handle and not INVALID_HANDLE_VALUE (proposal of unit
 * rt-shim-rows-2, held for the orchestrator). */
#define RT_HDEVINFO_EMPTY         ((HDEVINFO)(uintptr_t)0x00D1EC00u)

/* Flags of SetupDiGetClassDevs (setupapi.h; values as windows-sys 0.52.0 publishes them). */
#define DIGCF_DEFAULT             0x00000001u
#define DIGCF_PRESENT             0x00000002u
#define DIGCF_ALLCLASSES          0x00000004u
#define DIGCF_PROFILE             0x00000008u
#define DIGCF_DEVICEINTERFACE     0x00000010u

/* SP_DEVICE_INTERFACE_DATA: 32 bytes on x64 (setupapi.h packs to 8 there; windows-sys 0.52.0: natural layout on x64, packed(1) on x86 only). */
typedef struct {
    DWORD     cbSize;
    GUID      InterfaceClassGuid;
    DWORD     Flags;
    ULONG_PTR Reserved;
} SP_DEVICE_INTERFACE_DATA;
typedef SP_DEVICE_INTERFACE_DATA *PSP_DEVICE_INTERFACE_DATA;

_Static_assert(sizeof(SP_DEVICE_INTERFACE_DATA) == 32, "SP_DEVICE_INTERFACE_DATA");
_Static_assert(offsetof(SP_DEVICE_INTERFACE_DATA, Flags) == 20 &&
               offsetof(SP_DEVICE_INTERFACE_DATA, Reserved) == 24, "SP_DEVICE_INTERFACE_DATA offsets");

/* The device interface class of HID collections, what HidD_GetHidGuid stores:
 * {4D1E55B2-F16F-11CF-88CB-001111000030} (GUID_DEVINTERFACE_HID in Microsoft's driver
 * documentation; the value is windows-sys 0.52.0's GUID_DEVINTERFACE_HID). */
#define RT_GUID_DEVINTERFACE_HID \
    { 0x4D1E55B2u, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } }

/* bcrypt: an algorithm provider or key handle, and the flag that asks BCryptGenRandom for the
 * system's generator with a NULL handle (bcrypt.h; the value is on the function's page). */
typedef void *BCRYPT_ALG_HANDLE, *BCRYPT_KEY_HANDLE, *BCRYPT_HANDLE;
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG  0x00000002u

/* ---- added by unit rt-census-rows (docs/reviews/census-rows.md, part 1) ------------------ */

/* HIDD_ATTRIBUTES (hidsdi.h; Microsoft's page ns-hidsdi-_hidd_attributes): 12 bytes with the
 * tail padding.  "The caller must set the Size member."  No shim fills one: this machine has
 * no HID collection (HidD_GetAttributes is a stop). */
typedef struct { ULONG Size; USHORT VendorID, ProductID, VersionNumber; } HIDD_ATTRIBUTES;
typedef HIDD_ATTRIBUTES *PHIDD_ATTRIBUTES;
_Static_assert(sizeof(HIDD_ATTRIBUTES) == 12, "HIDD_ATTRIBUTES");

/* SP_DEVICE_INTERFACE_DETAIL_DATA_A (setupapi.h; the layout is Wine's public include/setupapi.h):
 * a size and the device path; sizeof is 8 on x64, which is what a caller stores in cbSize. */
typedef struct { DWORD cbSize; CHAR DevicePath[1]; } SP_DEVICE_INTERFACE_DETAIL_DATA_A;
typedef SP_DEVICE_INTERFACE_DETAIL_DATA_A *PSP_DEVICE_INTERFACE_DETAIL_DATA_A;
_Static_assert(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A) == 8, "SP_DEVICE_INTERFACE_DETAIL_DATA_A");

/* bcrypt: the pointer the functions take for bytes, and the names and numbers rt/bcrypt.h
 * answers.  The strings are the SDK's identifiers as the recording shows the game passing them
 * (L"AES", L"RNG", L"ChainingMode", L"ObjectLength", L"OpaqueKeyBlob") and as Wine's public
 * include/bcrypt.h spells the rest; the blob magic and version are from memory of that header. */
typedef UCHAR *PUCHAR;
#define BCRYPT_AES_ALGORITHM          u"AES"
#define BCRYPT_RNG_ALGORITHM          u"RNG"
#define MS_PRIMITIVE_PROVIDER         u"Microsoft Primitive Provider"
#define BCRYPT_OBJECT_LENGTH          u"ObjectLength"
#define BCRYPT_BLOCK_LENGTH           u"BlockLength"
#define BCRYPT_KEY_LENGTHS            u"KeyLengths"
#define BCRYPT_KEY_STRENGTH           u"KeyStrength"
#define BCRYPT_CHAINING_MODE          u"ChainingMode"
#define BCRYPT_CHAIN_MODE_CBC         u"ChainingModeCBC"
#define BCRYPT_CHAIN_MODE_CFB         u"ChainingModeCFB"
#define BCRYPT_CHAIN_MODE_GCM         u"ChainingModeGCM"
#define BCRYPT_KEY_DATA_BLOB          u"KeyDataBlob"
#define BCRYPT_OPAQUE_KEY_BLOB        u"OpaqueKeyBlob"
#define BCRYPT_KEY_DATA_BLOB_MAGIC    0x4d42444bu      /* "KDBM" */
#define BCRYPT_KEY_DATA_BLOB_VERSION1 1u
typedef struct { ULONG dwMagic, dwVersion, cbKeyData; } BCRYPT_KEY_DATA_BLOB_HEADER;
typedef struct { ULONG dwMinLength, dwMaxLength, dwIncrement; } BCRYPT_KEY_LENGTHS_STRUCT;
_Static_assert(sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) == 12 && sizeof(BCRYPT_KEY_LENGTHS_STRUCT) == 12, "bcrypt structures");

#endif /* RT_WIN_DEVICES_H */
