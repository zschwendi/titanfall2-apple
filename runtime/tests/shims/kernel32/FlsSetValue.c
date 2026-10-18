#include "rt/test.h"
#include "rt/thread.h"

/* FlsSetValue writes no guest memory: the checks are the return values, the values read back
 * through FlsGetValue (guest-visible, through CALL), and last error.  The index comes from the
 * core (rt_fls_alloc, rt_fls_free, rt/thread.h); the expected values are the core's. */
TEST(kernel32, FlsSetValue)
{
    Ctx *ctx = T_CTX();
    DWORD a;

    T_CASE("a value set on an allocated index is read back");
    a = rt_fls_alloc(0);
    T_EQ(CALL(kernel32, FlsSetValue, a, (PVOID)(uintptr_t)0x217), TRUE);
    T_EQ((uintptr_t)CALL(kernel32, FlsGetValue, a), 0x217);

    T_CASE("FlsSetValue leaves last error alone on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FlsSetValue, a, (PVOID)(uintptr_t)0x111), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("index 0 and index 128 fail with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FlsSetValue, 0, (PVOID)(uintptr_t)0x217), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FlsSetValue, 128, (PVOID)(uintptr_t)0x217), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("an index in range that was never allocated stores the value and FlsGetValue returns it");
    T_EQ(CALL(kernel32, FlsSetValue, 127, (PVOID)(uintptr_t)0x217), TRUE);   /* 127 is not allocated here */
    T_EQ((uintptr_t)CALL(kernel32, FlsGetValue, 127), 0x217);

    rt_fls_free(ctx, a);
}
