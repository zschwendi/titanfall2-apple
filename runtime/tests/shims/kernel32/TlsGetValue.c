#include "rt/test.h"
#include "rt/thread.h"

/* An allocated index in the expansion range (64 to 1,087).  TlsAlloc hands out the lowest
 * free index, so allocate until one is at least 64, then free the lower ones: each was free
 * when taken, so no other user owns it. */
static DWORD alloc_expansion_index(void)
{
    DWORD low[64], x = 0;
    unsigned n = 0;

    for (;;) {
        x = rt_tls_alloc();
        if (x >= 64) break;
        low[n++] = x;
    }
    for (unsigned i = 0; i < n; i++) rt_tls_free(low[i]);
    return x;
}

TEST(kernel32, TlsGetValue)
{
    Ctx *ctx = T_CTX();
    DWORD a, b, e, f;

    T_CASE("a fresh slot reads 0 and clears last error");
    a = rt_tls_alloc();
    e = alloc_expansion_index();
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, a), 0);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, e), 0);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    T_CASE("a value written is read back, all 64 bits");
    rt_tls_set(ctx, a, UINT64_C(0x8877665544332211));
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, TlsGetValue, a), UINT64_C(0x8877665544332211));
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    rt_tls_set(ctx, e, UINT64_C(0xFEDCBA9876543210));
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, TlsGetValue, e), UINT64_C(0xFEDCBA9876543210));
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    T_CASE("a stored 0 reads back with last error cleared");
    rt_tls_set(ctx, a, 0);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, a), 0);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    T_CASE("two slots do not disturb each other");
    b = rt_tls_alloc();
    rt_tls_set(ctx, a, 0x11);
    rt_tls_set(ctx, b, 0x22);
    rt_tls_set(ctx, e, 0x33);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, a), 0x11);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, b), 0x22);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, e), 0x33);
    rt_tls_set(ctx, b, 0x44);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, a), 0x11);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, e), 0x33);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, b), 0x44);

    T_CASE("an index that is not allocated reads its slot without a check");
    f = rt_tls_alloc();
    rt_tls_set(ctx, f, 0x55);
    rt_tls_free(f);                     /* the slot is zeroed by the free */
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, f), 0);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    rt_tls_set(ctx, f, 0x66);           /* not allocated: the core does not check, so it stores */
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, f), 0x66);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    T_CASE("an index of 1,088 or more fails with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, 1088), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, (DWORD)-1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0x1234);       /* 1,087 is the last index in range */
    T_EQ((uintptr_t)CALL(kernel32, TlsGetValue, 1087), 0);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    rt_tls_free(a);
    rt_tls_free(b);
    rt_tls_free(e);
}
