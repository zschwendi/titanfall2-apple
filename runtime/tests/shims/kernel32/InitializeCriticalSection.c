#include "rt/test.h"
#include "rt/win/sync.h"

TEST(kernel32, InitializeCriticalSection)
{
    CRITICAL_SECTION cs;

    T_CASE("initialises a free section, with every field set whatever the memory held before");
    memset(&cs, 0xA5, sizeof cs);
    CALL(kernel32, InitializeCriticalSection, &cs);
    T_EQ((uint64_t)(uintptr_t)cs.DebugInfo, UINT64_MAX);
    T_EQ(cs.LockCount, -1);
    T_EQ(cs.RecursionCount, 0);
    T_EQ((uintptr_t)cs.OwningThread, 0);
    T_EQ((uintptr_t)cs.LockSemaphore, 0);        /* the wait word and its unused upper half: free */
    T_EQ(cs.SpinCount, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, InitializeCriticalSection, &cs);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
