#include "rt/test.h"
#include "rt/heap.h"
#include "rt/log.h"
#include "rt/win/memory.h"

/* HeapSetInformation writes no guest memory: the checks are the return values, the log line (it is
 * printed once per process, counted with T_LOG_COUNT), last error, and the stops.  The expected
 * values are the notes of this task (the two accepted classes and the stops, from the page). */
TEST(kernel32, HeapSetInformation)
{
    HANDLE h = rt_heap_process();
    ULONG two = 2, one = 1;
    unsigned n0;

    n0 = T_LOG_COUNT(RT_CH_IMPORT);

    T_CASE("class 1 returns TRUE for a NULL heap and for the process heap");
    T_EQ(CALL(kernel32, HeapSetInformation, NULL, HeapEnableTerminationOnCorruption, NULL, 0), TRUE);
    T_EQ(CALL(kernel32, HeapSetInformation, h, HeapEnableTerminationOnCorruption, NULL, 0), TRUE);

    T_CASE("class 0 with the value 2 returns TRUE on a live heap");
    T_EQ(CALL(kernel32, HeapSetInformation, h, HeapCompatibilityInformation, &two, 4), TRUE);

    T_CASE("the accepted calls log one line, once");
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT) - n0, 1);
    T_LOGGED(RT_CH_IMPORT, "HeapSetInformation: the class is accepted and nothing is changed");
    T_EQ(CALL(kernel32, HeapSetInformation, h, HeapEnableTerminationOnCorruption, NULL, 0), TRUE);
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT) - n0, 1);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, HeapSetInformation, NULL, HeapEnableTerminationOnCorruption, NULL, 0), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, HeapSetInformation, h, HeapCompatibilityInformation, &two, 4), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, HeapSetInformation, h, HeapOptimizeResources, NULL, 0), "HeapOptimizeResources");
    T_TRAPS(CALL(kernel32, HeapSetInformation, h, 2, NULL, 0), "unknown class");
    T_TRAPS(CALL(kernel32, HeapSetInformation, h, HeapCompatibilityInformation, &two, 8), "a length other than 4");
    T_TRAPS(CALL(kernel32, HeapSetInformation, h, HeapCompatibilityInformation, NULL, 4), "a NULL HeapInformation");
    T_TRAPS(CALL(kernel32, HeapSetInformation, h, HeapCompatibilityInformation, &one, 4), "a value other than 2");
    T_TRAPS(CALL(kernel32, HeapSetInformation, NULL, HeapCompatibilityInformation, &two, 4), "a handle that is not a live heap");
}
