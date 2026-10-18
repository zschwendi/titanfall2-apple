/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/rpcdce/nf-rpcdce-uuidfromstringa
 * RT-CASES:  lower-case string gives the members; upper-case string gives the members; NULL string stores the nil UUID; wrong length fails and leaves Uuid untouched; non-hex digit fails and leaves Uuid untouched; hyphen in the wrong place fails and leaves Uuid untouched; does not change last error
 * RT-TRAPS:  UuidFromStringA with NULL Uuid
 */
#include "rt/shim.h"
#include "rt/win/com.h"

/* Static storage: every byte is zero. */
static const UUID uuid_nil;

/* Value of one hexadecimal digit of either case, or -1. */
static int uuid_hex_value(unsigned c)
{
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

/* Parses xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36 characters, hyphens at 8, 13, 18 and 23,
 * hexadecimal digits of either case elsewhere) into *out.  Returns 0 for anything else, and
 * then *out is not written.  The string ends at its first NUL, and no byte past that is read. */
static int uuid_parse_string(const BYTE *s, UUID *out)
{
    int digits[32];
    unsigned n = 0, i;

    for (i = 0; i < 36; i++) {
        if (s[i] == 0) return 0;                          /* shorter than 36 characters */
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return 0;
            continue;
        }
        digits[n] = uuid_hex_value(s[i]);
        if (digits[n] < 0) return 0;
        n++;
    }
    if (s[36] != 0) return 0;                             /* longer than 36 characters */

    /* The 32 hexadecimal digits in order: Data1, Data2, Data3, then Data4 in pairs. */
    out->Data1 = 0;
    for (i = 0; i < 8; i++) out->Data1 = (out->Data1 << 4) | (DWORD)digits[i];
    out->Data2 = (WORD)(digits[8] << 12 | digits[9] << 8 | digits[10] << 4 | digits[11]);
    out->Data3 = (WORD)(digits[12] << 12 | digits[13] << 8 | digits[14] << 4 | digits[15]);
    for (i = 0; i < 8; i++) out->Data4[i] = (BYTE)(digits[16 + 2 * i] << 4 | digits[17 + 2 * i]);
    return 1;
}

SHIM(rpcrt4, RPC_STATUS, UuidFromStringA, RPC_CSTR StringUuid, UUID *Uuid)
{
    UUID out;

    if (!Uuid) RT_UNIMPLEMENTED(ctx, "UuidFromStringA with NULL Uuid");
    if (!StringUuid) {
        *Uuid = uuid_nil;
        return RPC_S_OK;
    }
    if (!uuid_parse_string(StringUuid, &out)) return RPC_S_INVALID_STRING_UUID;
    *Uuid = out;
    return RPC_S_OK;
}
