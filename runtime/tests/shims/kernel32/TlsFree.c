#include "rt/test.h"
#include "rt/thread.h"
#include "rt/win/error.h"       /* ERROR_INVALID_PARAMETER */
#include "rt/win/process.h"     /* TLS_OUT_OF_INDEXES */

/* The core's indices are 0 to 1,087 (rt/thread.h).  The indices are allocated and freed through
 * the TlsAlloc and TlsFree shims; the slots are read and written through the core (rt_tls_get,
 * rt_tls_set), which is the storage that TlsGetValue and TlsSetValue read and write. */
#define T_TLS_INDICES 1088

/* Sets the slot of an allocated index, frees the index, and checks that the next TlsAlloc
 * gives the same index back with a zero slot.  The index is allocated again on return. */
static void free_and_reuse(DWORD idx)
{
    T_TRUE(rt_tls_set(T_CTX(), idx, 0x5555));
    T_EQ(CALL(kernel32, TlsFree, idx), TRUE);
    /* Every index below idx stays allocated, so idx is the lowest free one again. */
    T_EQ(CALL(kernel32, TlsAlloc), idx);
    T_EQ(rt_tls_get(T_CTX(), idx), 0);
}

TEST(kernel32, TlsFree)
{
    DWORD x[70], v;
    unsigned i;

    T_CASE("frees an allocated index");
    v = CALL(kernel32, TlsAlloc);
    T_EQ(CALL(kernel32, TlsFree, v), TRUE);

    T_CASE("a freed index is given out again, with a zero slot");
    /* Seventy indices: x[i] is at least i, so x[66] is in the expansion array (indices 64 and
     * up) and x[2] in the thread block. */
    for (i = 0; i < 70; i++)
        x[i] = CALL(kernel32, TlsAlloc);
    T_TRUE(x[66] >= 64);
    free_and_reuse(x[2]);
    free_and_reuse(x[66]);
    for (i = 0; i < 70; i++)
        T_EQ(CALL(kernel32, TlsFree, x[i]), TRUE);

    T_CASE("freeing an index that is not allocated fails");
    v = CALL(kernel32, TlsAlloc);
    T_EQ(CALL(kernel32, TlsFree, v), TRUE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, TlsFree, v), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("freeing an index of 1,088 or more fails");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, TlsFree, T_TLS_INDICES), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, TlsFree, TLS_OUT_OF_INDEXES), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    v = CALL(kernel32, TlsAlloc);
    T_EQ(CALL(kernel32, TlsFree, v), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
