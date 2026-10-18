#include "rt/test.h"
#include "rt/win/sync.h"

TEST(kernel32, EnterCriticalSection)
{
    CRITICAL_SECTION cs;
    DWORD self;

    T_CASE("enters a free section, setting owner, recursion count and wait word");
    CALL(kernel32, InitializeCriticalSection, &cs);
    self = CALL(kernel32, GetCurrentThreadId);
    CALL(kernel32, EnterCriticalSection, &cs);
    T_EQ(cs.LockCount, 0);
    T_EQ(cs.RecursionCount, 1);
    T_EQ((uintptr_t)cs.OwningThread, (uintptr_t)self);
    T_EQ((uint32_t)(uintptr_t)cs.LockSemaphore, 1);    /* the wait word: held */

    T_CASE("enters a section the caller owns again, raising the recursion count");
    CALL(kernel32, EnterCriticalSection, &cs);
    T_EQ(cs.RecursionCount, 2);
    T_EQ((uintptr_t)cs.OwningThread, (uintptr_t)self);
    T_EQ((uint32_t)(uintptr_t)cs.LockSemaphore, 1);
    CALL(kernel32, EnterCriticalSection, &cs);
    T_EQ(cs.RecursionCount, 3);

    T_CASE("a section never initialised or deleted stops the run");
    memset(&cs, 0, sizeof cs);                         /* DebugInfo 0: never initialised */
    T_TRAPS(CALL(kernel32, EnterCriticalSection, &cs), "never initialised, or was deleted");
    CALL(kernel32, InitializeCriticalSection, &cs);
    CALL(kernel32, DeleteCriticalSection, &cs);        /* zeroed again: deleted */
    T_TRAPS(CALL(kernel32, EnterCriticalSection, &cs), "never initialised, or was deleted");

    T_CASE("does not change last error");
    CALL(kernel32, InitializeCriticalSection, &cs);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, EnterCriticalSection, &cs);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
