#include "rt/test.h"
#include "rt/win/sync.h"

TEST(kernel32, TryEnterCriticalSection)
{
    CRITICAL_SECTION cs;
    DWORD self, other;

    T_CASE("enters a free section and returns TRUE");
    CALL(kernel32, InitializeCriticalSection, &cs);
    self = CALL(kernel32, GetCurrentThreadId);
    T_EQ(CALL(kernel32, TryEnterCriticalSection, &cs), TRUE);
    T_EQ(cs.LockCount, 0);
    T_EQ(cs.RecursionCount, 1);
    T_EQ((uintptr_t)cs.OwningThread, (uintptr_t)self);
    T_EQ((uint32_t)(uintptr_t)cs.LockSemaphore, 1);    /* the wait word: held */

    T_CASE("enters a section the caller owns and returns TRUE, raising the recursion count");
    T_EQ(CALL(kernel32, TryEnterCriticalSection, &cs), TRUE);
    T_EQ(cs.RecursionCount, 2);
    T_EQ((uintptr_t)cs.OwningThread, (uintptr_t)self);
    T_EQ((uint32_t)(uintptr_t)cs.LockSemaphore, 1);

    /* Held by another thread: the state EnterCriticalSection would leave in that thread,
     * set by hand (rt/lock.h, 6.4).  The shim must not wait, and must change nothing. */
    other = self + 0x100;
    T_CASE("a section held by another thread returns FALSE and changes nothing");
    CALL(kernel32, InitializeCriticalSection, &cs);
    cs.OwningThread = (HANDLE)(uintptr_t)other;
    cs.RecursionCount = 1;
    cs.LockCount = 0;
    cs.LockSemaphore = (HANDLE)(uintptr_t)1;
    T_EQ(CALL(kernel32, TryEnterCriticalSection, &cs), FALSE);
    T_EQ(CALL(kernel32, TryEnterCriticalSection, &cs), FALSE);
    T_EQ((uintptr_t)cs.OwningThread, (uintptr_t)other);
    T_EQ(cs.RecursionCount, 1);
    T_EQ(cs.LockCount, 0);
    T_EQ((uint32_t)(uintptr_t)cs.LockSemaphore, 1);

    T_CASE("a section never initialised or deleted stops the run");
    memset(&cs, 0, sizeof cs);                         /* DebugInfo 0: never initialised */
    T_TRAPS(CALL(kernel32, TryEnterCriticalSection, &cs), "never initialised, or was deleted");
    CALL(kernel32, InitializeCriticalSection, &cs);
    CALL(kernel32, DeleteCriticalSection, &cs);        /* zeroed again: deleted */
    T_TRAPS(CALL(kernel32, TryEnterCriticalSection, &cs), "never initialised, or was deleted");

    T_CASE("does not change last error");
    CALL(kernel32, InitializeCriticalSection, &cs);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, TryEnterCriticalSection, &cs), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, InitializeCriticalSection, &cs);
    cs.OwningThread = (HANDLE)(uintptr_t)other;
    cs.RecursionCount = 1;
    cs.LockCount = 0;
    cs.LockSemaphore = (HANDLE)(uintptr_t)1;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, TryEnterCriticalSection, &cs), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
