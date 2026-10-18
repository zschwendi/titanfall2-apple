#include "rt/test.h"
#include "rt/thread.h"

/* The core's indices are 0 to 1,087 (rt/thread.h).  This test frees with the core, not the
 * TlsFree shim, so that it tests this shim alone. */
#define T_TLS_INDICES 1088

/* Allocates until the core stops.  T_TRAPS runs it in a forked child, so the indices this
 * test holds are untouched in the parent.  The loop is bounded: if the core did not stop
 * within one call more than the 1,088 indices, the trap check fails. */
static void tls_alloc_until_stop(void)
{
    unsigned i;

    for (i = 0; i <= T_TLS_INDICES; i++)
        CALL(kernel32, TlsAlloc);
}

TEST(kernel32, TlsAlloc)
{
    DWORD x[8], v;
    unsigned i;

    T_CASE("returns the lowest index that is not allocated");
    for (i = 0; i < 8; i++) {
        x[i] = CALL(kernel32, TlsAlloc);
        T_TRUE(x[i] < T_TLS_INDICES);
        if (i > 0) T_TRUE(x[i] > x[i - 1]);     /* every index below x[i - 1] is still taken */
    }
    /* Free two: the lower one is given out first, then the higher one. */
    T_TRUE(rt_tls_free(x[6]));
    T_TRUE(rt_tls_free(x[3]));
    T_EQ(CALL(kernel32, TlsAlloc), x[3]);
    T_EQ(CALL(kernel32, TlsAlloc), x[6]);
    for (i = 0; i < 8; i++)
        T_TRUE(rt_tls_free(x[i]));

    T_CASE("the index it returns has a zero slot");
    v = CALL(kernel32, TlsAlloc);
    T_EQ(rt_tls_get(T_CTX(), v), 0);            /* never written: zero */
    T_TRUE(rt_tls_set(T_CTX(), v, 0x5555));
    T_TRUE(rt_tls_free(v));
    /* The freed index is the lowest free one again, and its slot was zeroed by the free. */
    T_EQ(CALL(kernel32, TlsAlloc), v);
    T_EQ(rt_tls_get(T_CTX(), v), 0);
    T_TRUE(rt_tls_free(v));

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    v = CALL(kernel32, TlsAlloc);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(rt_tls_free(v));

    /* All 1,088 in use: the core stops, so the shim adds no guard (RT-TRAPS).  Last, after the
     * checks of the last case. */
    T_TRAPS(tls_alloc_until_stop(), "all 1088 TLS indices are allocated");
}
