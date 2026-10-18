/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  a shim that returns with the guest stack pointer moved is stopped by its thunk
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

/* Deliberately wrong when asked: a shim must leave ctx->r[4] alone.  The generated thunk
 * checks that on the way out, where the culprit is still known. */
SHIM_RAW(rttest, MovesRsp)
{
    ctx->r[4] += ARG_U64(ctx, 0);
    RET_U64(ctx, 1);
}
