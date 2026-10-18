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

/* The value the guest reads back from its slot through the TlsGetValue shim. */
static uint64_t get_value(DWORD index)
{
    return (uint64_t)(uintptr_t)CALL(kernel32, TlsGetValue, index);
}

TEST(kernel32, TlsSetValue)
{
    DWORD a, b, e, f;

    a = rt_tls_alloc();
    b = rt_tls_alloc();
    e = alloc_expansion_index();

    T_CASE("a value written is read back, all 64 bits");
    T_EQ(CALL(kernel32, TlsSetValue, a, (LPVOID)(uintptr_t)UINT64_C(0x8877665544332211)), TRUE);
    T_EQ(get_value(a), UINT64_C(0x8877665544332211));
    T_EQ(CALL(kernel32, TlsSetValue, a, (LPVOID)(uintptr_t)UINT64_C(0xFFFFFFFFFFFFFFFF)), TRUE);
    T_EQ(get_value(a), UINT64_C(0xFFFFFFFFFFFFFFFF));
    T_EQ(CALL(kernel32, TlsSetValue, e, (LPVOID)(uintptr_t)UINT64_C(0xFEDCBA9876543210)), TRUE);
    T_EQ(get_value(e), UINT64_C(0xFEDCBA9876543210));

    T_CASE("two slots do not disturb each other");
    T_EQ(CALL(kernel32, TlsSetValue, a, (LPVOID)0x11), TRUE);
    T_EQ(CALL(kernel32, TlsSetValue, b, (LPVOID)0x22), TRUE);
    T_EQ(CALL(kernel32, TlsSetValue, e, (LPVOID)0x33), TRUE);
    T_EQ(get_value(a), 0x11);
    T_EQ(get_value(b), 0x22);
    T_EQ(get_value(e), 0x33);
    T_EQ(CALL(kernel32, TlsSetValue, b, (LPVOID)0x44), TRUE);
    T_EQ(get_value(a), 0x11);
    T_EQ(get_value(e), 0x33);
    T_EQ(get_value(b), 0x44);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, TlsSetValue, a, (LPVOID)0x55), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, TlsSetValue, e, NULL), TRUE);      /* a stored 0 is a success too */
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("an index that is not allocated stores its value");
    f = rt_tls_alloc();
    rt_tls_free(f);                     /* f is no longer allocated */
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, TlsSetValue, f, (LPVOID)0x66), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(get_value(f), 0x66);

    T_CASE("an index of 1,088 or more fails with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, TlsSetValue, 1088, (LPVOID)0x77), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, TlsSetValue, (DWORD)-1, (LPVOID)0x77), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0x1234);       /* 1,087 is the last index in range */
    T_EQ(CALL(kernel32, TlsSetValue, 1087, (LPVOID)0x77), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    rt_tls_free(a);
    rt_tls_free(b);
    rt_tls_free(e);
}
