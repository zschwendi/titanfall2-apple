/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/rpcdce/nf-rpcdce-uuidtostringa
 * RT-CASES:  converts a UUID to 36 lower-case characters and a NUL; a NULL UUID is the nil UUID; does not change last error
 * RT-TRAPS:  NULL StringUuid
 */
#include "rt/shim.h"
#include "rt/heap.h"
#include "rt/win/com.h"

/* Writes the `digits` low nibbles of value as lower-case hex digits, most significant first,
 * and returns the position after them.  stdio.h is not available to a shim: a table. */
static char *put_hex(char *out, uint64_t value, unsigned digits)
{
    static const char table[] = "0123456789abcdef";
    for (unsigned i = 0; i < digits; i++)
        out[i] = table[(value >> (4 * (digits - 1 - i))) & 0xF];
    return out + digits;
}

SHIM(rpcrt4, RPC_STATUS, UuidToStringA, const UUID *Uuid, RPC_CSTR *StringUuid)
{
    static const UUID nil_uuid;                     /* all zeros: the nil UUID */
    const UUID *u = Uuid ? Uuid : &nil_uuid;
    char *text, *p;
    unsigned i;

    if (!StringUuid)
        RT_UNIMPLEMENTED(ctx, "UuidToStringA with a NULL StringUuid");
    text = rt_heap_alloc(rt_heap_process(), 0, 37);
    if (!text)
        return RPC_S_OUT_OF_MEMORY;
    p = put_hex(text, u->Data1, 8);
    *p++ = '-';
    p = put_hex(p, u->Data2, 4);
    *p++ = '-';
    p = put_hex(p, u->Data3, 4);
    *p++ = '-';
    p = put_hex(p, ((uint64_t)u->Data4[0] << 8) | u->Data4[1], 4);
    *p++ = '-';
    for (i = 2; i < 8; i++)
        p = put_hex(p, u->Data4[i], 2);
    *p = '\0';
    *StringUuid = (RPC_CSTR)text;
    return RPC_S_OK;
}
