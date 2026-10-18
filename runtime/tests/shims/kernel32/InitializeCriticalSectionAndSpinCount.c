#include "rt/test.h"
#include "rt/win/sync.h"

TEST(kernel32, InitializeCriticalSectionAndSpinCount)
{
    CRITICAL_SECTION cs;

    T_CASE("initialises a free section with the spin count, whatever the memory held before");
    memset(&cs, 0xA5, sizeof cs);
    T_EQ(CALL(kernel32, InitializeCriticalSectionAndSpinCount, &cs, 4000), TRUE);
    T_EQ((uint64_t)(uintptr_t)cs.DebugInfo, UINT64_MAX);
    T_EQ(cs.LockCount, -1);
    T_EQ(cs.RecursionCount, 0);
    T_EQ((uintptr_t)cs.OwningThread, 0);
    T_EQ((uintptr_t)cs.LockSemaphore, 0);        /* the wait word and its unused upper half: free */
    T_EQ(cs.SpinCount, 4000);

    T_CASE("keeps the low 24 bits of the spin count and drops the high byte");
    CALL(kernel32, InitializeCriticalSectionAndSpinCount, &cs, 0x80000FA0);
    T_EQ(cs.SpinCount, 0xFA0);
    CALL(kernel32, InitializeCriticalSectionAndSpinCount, &cs, 0xFFFFFFFF);
    T_EQ(cs.SpinCount, 0x00FFFFFF);
    CALL(kernel32, InitializeCriticalSectionAndSpinCount, &cs, 0);
    T_EQ(cs.SpinCount, 0);

    T_CASE("returns TRUE for every spin count");
    T_EQ(CALL(kernel32, InitializeCriticalSectionAndSpinCount, &cs, 0), TRUE);
    T_EQ(CALL(kernel32, InitializeCriticalSectionAndSpinCount, &cs, 1), TRUE);
    T_EQ(CALL(kernel32, InitializeCriticalSectionAndSpinCount, &cs, 0x80000000), TRUE);
    T_EQ(CALL(kernel32, InitializeCriticalSectionAndSpinCount, &cs, 0xFFFFFFFF), TRUE);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, InitializeCriticalSectionAndSpinCount, &cs, 4000);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
