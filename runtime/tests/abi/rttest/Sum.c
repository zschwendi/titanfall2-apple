/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  a varargs function fetches its own arguments from registers and stack
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

/* ULONGLONG Sum(DWORD count, ...): adds `count` 64-bit arguments. */
SHIM_RAW(rttest, Sum)
{
    DWORD count = (DWORD)ARG_U64(ctx, 0);
    ULONGLONG total = 0;
    for (DWORD i = 0; i < count; i++) total += ARG_U64(ctx, 1 + i);
    RET_U64(ctx, total);
}
