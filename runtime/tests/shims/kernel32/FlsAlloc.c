#include "rt/test.h"
#include "rt/thread.h"

/* FlsAlloc writes no guest memory: the checks are the returned indexes, the reuse of a freed index,
 * and last error.  The expected values are the core's (rt/thread.h): indexes from 1, nonzero,
 * the lowest free one given out.  The fatal stop when all 127 are allocated is reached here
 * (the core's message is named in T_TRAPS). */
TEST(kernel32, FlsAlloc)
{
    DWORD a, b, c, all[RT_FLS_SLOTS - 1];

    T_CASE("two allocations give two different indexes, neither 0, and leave last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    a = CALL(kernel32, FlsAlloc, 0);
    b = CALL(kernel32, FlsAlloc, 0);
    T_TRUE(a != 0);
    T_TRUE(b != 0);
    T_TRUE(a != b);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a freed index is given out again");
    T_EQ(CALL(kernel32, FlsFree, a), TRUE);
    c = CALL(kernel32, FlsAlloc, 0);
    T_EQ(c, a);                                       /* a was the lowest index free */
    T_EQ(CALL(kernel32, FlsFree, b), TRUE);
    T_EQ(CALL(kernel32, FlsFree, c), TRUE);

    /* Setup for the trap below: no index allocated, then all 127 allocated. */
    for (unsigned i = 0; i < RT_FLS_SLOTS - 1; i++) {
        all[i] = CALL(kernel32, FlsAlloc, 0);
        T_TRUE(all[i] != 0);
    }

    T_TRAPS(CALL(kernel32, FlsAlloc, 0), "all 127 FLS indices are allocated");

    for (unsigned i = 0; i < RT_FLS_SLOTS - 1; i++)
        T_EQ(CALL(kernel32, FlsFree, all[i]), TRUE);
}
