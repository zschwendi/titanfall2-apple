#include "rt/test.h"
#include "rt/thread.h"

/* The callback of the checks: it stores the value it is given (the first argument, ctx->r[1]) and
 * counts its calls. */
static uint64_t seen;
static unsigned calls;

GUEST_FN(record)
{
    seen = ctx->r[1];
    calls++;
    GUEST_RET(ctx);
}

/* FlsFree writes no guest memory: the checks are the return values, the callback calls (their
 * number and the value each got), and last error.  The expected values are the core's
 * (rt/thread.h). */
TEST(kernel32, FlsFree)
{
    Ctx *ctx = T_CTX();
    DWORD a;

    T_CASE("index 0, index 128 and an index that is not allocated fail with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FlsFree, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FlsFree, 128), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FlsFree, 127), FALSE);        /* nothing allocated yet in this test */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a nonzero value runs the callback once, with the value");
    a = CALL(kernel32, FlsAlloc, T_GUEST_ADDR(record));
    T_TRUE(a != 0);
    T_TRUE(rt_fls_set(ctx, a, 0x1234));
    calls = 0;
    seen = 0;
    T_EQ(CALL(kernel32, FlsFree, a), TRUE);
    T_EQ(calls, 1);
    T_EQ(seen, 0x1234);

    T_CASE("a zero value does not run the callback");
    a = CALL(kernel32, FlsAlloc, T_GUEST_ADDR(record));
    T_TRUE(a != 0);
    calls = 0;
    T_EQ(CALL(kernel32, FlsFree, a), TRUE);
    T_EQ(calls, 0);

    T_CASE("an index allocated with a NULL callback is freed without a call");
    a = CALL(kernel32, FlsAlloc, 0);
    T_TRUE(a != 0);
    T_TRUE(rt_fls_set(ctx, a, 0x55));
    calls = 0;
    T_EQ(CALL(kernel32, FlsFree, a), TRUE);
    T_EQ(calls, 0);

    T_CASE("a preset last error is left alone on success");
    a = CALL(kernel32, FlsAlloc, 0);
    T_TRUE(a != 0);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FlsFree, a), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
