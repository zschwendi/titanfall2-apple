#include "rt/test.h"
#include "rt/win/sync.h"

TEST(kernel32, InitializeCriticalSectionEx)
{
    CRITICAL_SECTION cs;

    T_CASE("initialises a free section with the spin count, whatever the memory held before");
    memset(&cs, 0xA5, sizeof cs);
    T_EQ(CALL(kernel32, InitializeCriticalSectionEx, &cs, 4000, 0), TRUE);
    T_EQ((uint64_t)(uintptr_t)cs.DebugInfo, UINT64_MAX);
    T_EQ(cs.LockCount, -1);
    T_EQ(cs.RecursionCount, 0);
    T_EQ((uintptr_t)cs.OwningThread, 0);
    T_EQ((uintptr_t)cs.LockSemaphore, 0);        /* the wait word and its unused upper half: free */
    T_EQ(cs.SpinCount, 4000);

    T_CASE("flags 0 and CRITICAL_SECTION_NO_DEBUG_INFO both initialise the section");
    memset(&cs, 0x5A, sizeof cs);
    T_EQ(CALL(kernel32, InitializeCriticalSectionEx, &cs, 0, CRITICAL_SECTION_NO_DEBUG_INFO), TRUE);
    T_EQ((uint64_t)(uintptr_t)cs.DebugInfo, UINT64_MAX);
    T_EQ(cs.LockCount, -1);
    T_EQ(cs.RecursionCount, 0);
    T_EQ((uintptr_t)cs.OwningThread, 0);
    T_EQ((uintptr_t)cs.LockSemaphore, 0);
    T_EQ(cs.SpinCount, 0);
    memset(&cs, 0x5A, sizeof cs);
    T_EQ(CALL(kernel32, InitializeCriticalSectionEx, &cs, 0, 0), TRUE);
    T_EQ((uint64_t)(uintptr_t)cs.DebugInfo, UINT64_MAX);
    T_EQ(cs.LockCount, -1);
    /* Any flag bit the documentation does not list stops the run; nothing is written. */
    T_TRAPS(CALL(kernel32, InitializeCriticalSectionEx, &cs, 0, 0x2), "with Flags 0x2 (only");
    T_TRAPS(CALL(kernel32, InitializeCriticalSectionEx, &cs, 0, 0x01000001), "with Flags 0x1000001 (only");
    T_TRAPS(CALL(kernel32, InitializeCriticalSectionEx, &cs, 0, 0x80000000), "with Flags 0x80000000 (only");

    /* Where the expected value comes from: the recording of the real game's start has this
     * call twice (engine+0x2564ce, arguments (section, 0, 1), 21.11 s), result 1 both times,
     * and the engine goes on to enter the section (docs/handoff/gate3-run-8.md, entry 1). */
    T_CASE("the value 1 that the engine passes initialises the section as 0 does");
    memset(&cs, 0x5A, sizeof cs);
    T_EQ(CALL(kernel32, InitializeCriticalSectionEx, &cs, 0, 1), TRUE);
    T_EQ((uint64_t)(uintptr_t)cs.DebugInfo, UINT64_MAX);
    T_EQ(cs.LockCount, -1);
    T_EQ(cs.RecursionCount, 0);
    T_EQ((uintptr_t)cs.OwningThread, 0);
    T_EQ((uintptr_t)cs.LockSemaphore, 0);
    T_EQ(cs.SpinCount, 0);
    CALL(kernel32, EnterCriticalSection, &cs);
    T_EQ(cs.RecursionCount, 1);
    CALL(kernel32, LeaveCriticalSection, &cs);
    T_EQ(cs.RecursionCount, 0);
    T_TRAPS(CALL(kernel32, InitializeCriticalSectionEx, &cs, 0, 0x3), "with Flags 0x3 (only");

    T_CASE("keeps the low 24 bits of the spin count and drops the high byte");
    CALL(kernel32, InitializeCriticalSectionEx, &cs, 0x80000FA0, 0);
    T_EQ(cs.SpinCount, 0xFA0);
    CALL(kernel32, InitializeCriticalSectionEx, &cs, 0xFFFFFFFF, CRITICAL_SECTION_NO_DEBUG_INFO);
    T_EQ(cs.SpinCount, 0x00FFFFFF);

    T_CASE("returns TRUE");
    T_EQ(CALL(kernel32, InitializeCriticalSectionEx, &cs, 0, 0), TRUE);
    T_EQ(CALL(kernel32, InitializeCriticalSectionEx, &cs, 4000, CRITICAL_SECTION_NO_DEBUG_INFO), TRUE);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, InitializeCriticalSectionEx, &cs, 4000, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
