/* GUIDs and the few COM and RPC names the ole32, rpcrt4, hid and setupapi shims use
 * (guiddef.h, objbase.h, rpcdce.h).  There is no COM run time in this runtime before Gate 3
 * (docs/RUNTIME.md sections 3.7 and 11.1): no class can be created, so no interface is
 * declared here.  Values checked against Wine's include/ (winerror.h, objbase.h) where the
 * comment says so; the rest is from memory of the SDK headers. */
#ifndef RT_WIN_COM_H
#define RT_WIN_COM_H

#include <stddef.h>
#include "rt/wintypes.h"

/* GUID: 16 bytes, Data1 to Data3 in the machine's byte order (little endian), Data4 as bytes.
 * The text form "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" prints Data1, Data2, Data3, then
 * Data4[0..1], then Data4[2..7]. */
typedef struct {
    DWORD Data1;
    WORD  Data2;
    WORD  Data3;
    BYTE  Data4[8];
} GUID;
typedef GUID *LPGUID;
typedef const GUID *LPCGUID;
typedef GUID UUID, CLSID, IID;
typedef const GUID *REFGUID, *REFCLSID, *REFIID;     /* C has no references: a pointer in the ABI */

_Static_assert(sizeof(GUID) == 16, "GUID");
_Static_assert(offsetof(GUID, Data2) == 4 && offsetof(GUID, Data3) == 6 && offsetof(GUID, Data4) == 8,
               "GUID offsets");

/* An interface pointer the runtime never follows. */
typedef void *LPUNKNOWN;

/* dwCoInit of CoInitializeEx (objbase.h; values as windows-sys 0.52.0 publishes them). */
#define COINIT_MULTITHREADED      0x0u
#define COINIT_APARTMENTTHREADED  0x2u
#define COINIT_DISABLE_OLE1DDE    0x4u
#define COINIT_SPEED_OVER_MEMORY  0x8u

/* rpcrt4: RPC_STATUS is `long` in the SDK, 32 bits on Windows; a string is unsigned char *. */
typedef LONG RPC_STATUS;
typedef BYTE *RPC_CSTR;

#endif /* RT_WIN_COM_H */
