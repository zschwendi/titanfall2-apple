#include "rt/test.h"
#include "rt/thread.h"

/* FlsGetValue writes no guest memory: the checks are the values read, last error, and the failures
 * of indexes 0 and 128.  The values are stored and the indexes come from the core (rt_fls_set,
 * rt_fls_alloc, rt_fls_free, rt/thread.h); the expected values are the core's: NULL for a new
 * index, ERROR_SUCCESS on success, ERROR_INVALID_PARAMETER on failure. */
TEST(kernel32, FlsGetValue)
{
    Ctx *ctx = T_CTX();
    DWORD a, b;

    T_CASE("a new index reads NULL with last error ERROR_SUCCESS");
    a = rt_fls_alloc(0);
    CALL(kernel32, SetLastError, 0xDEADBEEF);
    T_EQ((uintptr_t)CALL(kernel32, FlsGetValue, a), 0);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    T_CASE("a value set reads back with last error ERROR_SUCCESS, on two indexes independently");
    b = rt_fls_alloc(0);
    T_TRUE(rt_fls_set(ctx, a, 0x217));
    T_TRUE(rt_fls_set(ctx, b, 0x317));
    CALL(kernel32, SetLastError, 0xDEADBEEF);
    T_EQ((uintptr_t)CALL(kernel32, FlsGetValue, a), 0x217);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0xDEADBEEF);
    T_EQ((uintptr_t)CALL(kernel32, FlsGetValue, b), 0x317);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    T_CASE("index 0 and index 128 fail with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0xDEADBEEF);
    T_EQ((uintptr_t)CALL(kernel32, FlsGetValue, 0), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0xDEADBEEF);
    T_EQ((uintptr_t)CALL(kernel32, FlsGetValue, 128), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("an index in range that was never allocated returns the value stored for it");
    T_TRUE(rt_fls_set(ctx, 127, 0x217));                 /* 127 is not allocated in this test */
    CALL(kernel32, SetLastError, 0xDEADBEEF);
    T_EQ((uintptr_t)CALL(kernel32, FlsGetValue, 127), 0x217);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    rt_fls_free(ctx, a);
    rt_fls_free(ctx, b);
}
