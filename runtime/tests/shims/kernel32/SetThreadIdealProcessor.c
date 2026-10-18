#include "rt/test.h"
#include "rt/machine.h"
#include "rt/thread.h"

/* (HANDLE)-2 is the calling thread (rt/thread.h).  A new thread starts with ideal processor 0,
 * so the first call in this test sees 0 as the previous value. */
TEST(kernel32, SetThreadIdealProcessor)
{
    HANDLE self = (HANDLE)(intptr_t)-2;
    uint32_t n = rt_machine()->cpu_count;

    T_CASE("stores a processor below the machine's count and returns the previous one, 0 for a new thread");
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 0), (DWORD)0);
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 0), (DWORD)0);
    if (n > 1) {
        T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, n - 1), (DWORD)0);
        T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 0), (DWORD)(n - 1));
    }

    T_CASE("MAXIMUM_PROCESSORS returns the stored processor and changes nothing");
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 64), (DWORD)0);
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 64), (DWORD)0);
    CALL(kernel32, SetThreadIdealProcessor, self, 0);
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 64), (DWORD)0);

    T_CASE("a processor above MAXIMUM_PROCESSORS fails with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 65), (DWORD)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 64), (DWORD)0);   /* the stored value is unchanged */

    T_CASE("NULL fails with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, NULL, 0), (DWORD)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 0), (DWORD)0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 64), (DWORD)0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stop: a processor from the machine's count up to 63 (core, rt/thread.h). */
    if (n < 64) {
        T_TRAPS(CALL(kernel32, SetThreadIdealProcessor, self, n), "rt_thread_set_ideal_processor: not implemented");
    }
    T_EQ(CALL(kernel32, SetThreadIdealProcessor, self, 0), (DWORD)0);     /* leave the value at 0 */
}
