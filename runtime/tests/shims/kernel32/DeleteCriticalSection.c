#include "rt/test.h"
#include "rt/win/sync.h"

static int all_zero(const void *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (((const unsigned char *)p)[i]) return 0;
    return 1;
}

TEST(kernel32, DeleteCriticalSection)
{
    CRITICAL_SECTION cs;
    unsigned n;

    T_CASE("zeroes all 40 bytes of a free section and logs nothing");
    CALL(kernel32, InitializeCriticalSection, &cs);
    n = T_LOG_COUNT(RT_CH_THREAD);
    CALL(kernel32, DeleteCriticalSection, &cs);
    T_EQ(sizeof cs, 40);
    T_TRUE(all_zero(&cs, sizeof cs));
    T_EQ(T_LOG_COUNT(RT_CH_THREAD), n);

    T_CASE("zeroes a held section and logs that it was held");
    CALL(kernel32, InitializeCriticalSection, &cs);
    DWORD self = CALL(kernel32, GetCurrentThreadId);
    /* The state EnterCriticalSection leaves: held once by the calling thread, wait word 1.
     * Set by hand here, since the Enter shim is built after this one (rt/lock.h, 6.4). */
    cs.OwningThread = (HANDLE)(uintptr_t)self;
    cs.RecursionCount = 1;
    cs.LockCount = 0;
    cs.LockSemaphore = (HANDLE)(uintptr_t)1;
    CALL(kernel32, DeleteCriticalSection, &cs);
    T_TRUE(all_zero(&cs, sizeof cs));
    T_TRUE(T_LOGGED(RT_CH_THREAD, "DeleteCriticalSection("));
    T_TRUE(T_LOGGED(RT_CH_THREAD, "the section is held by thread"));

    T_CASE("a deleted section can be initialised again");
    CALL(kernel32, InitializeCriticalSection, &cs);
    CALL(kernel32, DeleteCriticalSection, &cs);
    CALL(kernel32, InitializeCriticalSection, &cs);
    T_EQ((uint64_t)(uintptr_t)cs.DebugInfo, UINT64_MAX);
    T_EQ(cs.LockCount, -1);
    T_EQ((uintptr_t)cs.LockSemaphore, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, DeleteCriticalSection, &cs);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
