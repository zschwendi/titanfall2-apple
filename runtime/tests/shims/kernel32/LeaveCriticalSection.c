#include "rt/test.h"
#include "rt/win/sync.h"

TEST(kernel32, LeaveCriticalSection)
{
    CRITICAL_SECTION cs;
    DWORD self, other;

    T_CASE("leaves a section held once: freed, owner cleared, wait word 0");
    CALL(kernel32, InitializeCriticalSection, &cs);
    self = CALL(kernel32, GetCurrentThreadId);
    CALL(kernel32, EnterCriticalSection, &cs);
    CALL(kernel32, LeaveCriticalSection, &cs);
    T_EQ(cs.LockCount, -1);
    T_EQ(cs.RecursionCount, 0);
    T_EQ((uintptr_t)cs.OwningThread, 0);
    T_EQ((uint32_t)(uintptr_t)cs.LockSemaphore, 0);

    T_CASE("leaves one of two holds: still owned, count down by one");
    CALL(kernel32, EnterCriticalSection, &cs);
    CALL(kernel32, EnterCriticalSection, &cs);
    CALL(kernel32, LeaveCriticalSection, &cs);
    T_EQ(cs.RecursionCount, 1);
    T_EQ(cs.LockCount, 0);
    T_EQ((uintptr_t)cs.OwningThread, (uintptr_t)self);
    T_EQ((uint32_t)(uintptr_t)cs.LockSemaphore, 1);    /* still held */
    CALL(kernel32, LeaveCriticalSection, &cs);
    T_EQ(cs.RecursionCount, 0);
    T_EQ(cs.LockCount, -1);
    T_EQ((uintptr_t)cs.OwningThread, 0);

    T_CASE("a leave by a thread that does not own the section stops the run");
    /* Never entered: owner 0. */
    CALL(kernel32, InitializeCriticalSection, &cs);
    T_TRAPS(CALL(kernel32, LeaveCriticalSection, &cs), "does not own");
    /* Held by another thread: the state EnterCriticalSection would leave in that thread,
     * set by hand (rt/lock.h, 6.4).  The owner is not the calling thread. */
    other = self + 0x100;
    cs.OwningThread = (HANDLE)(uintptr_t)other;
    cs.RecursionCount = 1;
    cs.LockCount = 0;
    cs.LockSemaphore = (HANDLE)(uintptr_t)1;
    T_TRAPS(CALL(kernel32, LeaveCriticalSection, &cs), "does not own");
    /* One leave too many: freed, then left again. */
    CALL(kernel32, InitializeCriticalSection, &cs);
    CALL(kernel32, EnterCriticalSection, &cs);
    CALL(kernel32, LeaveCriticalSection, &cs);
    T_TRAPS(CALL(kernel32, LeaveCriticalSection, &cs), "does not own");

    T_CASE("does not change last error");
    CALL(kernel32, InitializeCriticalSection, &cs);
    CALL(kernel32, EnterCriticalSection, &cs);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, LeaveCriticalSection, &cs);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
