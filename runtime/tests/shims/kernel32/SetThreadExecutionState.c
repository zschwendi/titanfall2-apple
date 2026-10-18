#include "rt/test.h"
#include "rt/thread.h"
#include "rt/win/process.h"

/* SetThreadExecutionState writes no guest memory: the checks are the return value (the state
 * before the call, which the next call shows) and last error.  The expected values are those
 * of the core's comment in rt/thread.h (Microsoft's page of the function; the first value,
 * ES_CONTINUOUS alone, is marked "from memory" there). */
TEST(kernel32, SetThreadExecutionState)
{
    T_CASE("a first call with ES_CONTINUOUS | ES_DISPLAY_REQUIRED returns ES_CONTINUOUS");
    T_EQ(CALL(kernel32, SetThreadExecutionState, ES_CONTINUOUS | ES_DISPLAY_REQUIRED), ES_CONTINUOUS);

    T_CASE("a second call returns the flags of the first");
    T_EQ(CALL(kernel32, SetThreadExecutionState, ES_CONTINUOUS | ES_SYSTEM_REQUIRED), ES_CONTINUOUS | ES_DISPLAY_REQUIRED);

    T_CASE("a call without ES_CONTINUOUS returns the state and leaves it as it is");
    T_EQ(CALL(kernel32, SetThreadExecutionState, ES_DISPLAY_REQUIRED), ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    T_EQ(CALL(kernel32, SetThreadExecutionState, ES_CONTINUOUS), ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    T_CASE("ES_USER_PRESENT with another flag fails with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetThreadExecutionState, ES_USER_PRESENT | ES_CONTINUOUS), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(kernel32, SetThreadExecutionState, ES_CONTINUOUS | ES_AWAYMODE_REQUIRED), ES_CONTINUOUS);   /* nothing was recorded */

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetThreadExecutionState, ES_CONTINUOUS), ES_CONTINUOUS | ES_AWAYMODE_REQUIRED);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, SetThreadExecutionState, ES_USER_PRESENT), "ES_USER_PRESENT");
    T_TRAPS(CALL(kernel32, SetThreadExecutionState, ES_AWAYMODE_REQUIRED), "ES_AWAYMODE_REQUIRED");
    T_TRAPS(CALL(kernel32, SetThreadExecutionState, ES_CONTINUOUS | 0x100), "outside the five ES_ values");
}
