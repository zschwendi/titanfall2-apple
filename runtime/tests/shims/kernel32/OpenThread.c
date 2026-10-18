#include "rt/test.h"
#include "rt/handle.h"

/* No thread is started: the test's own id names the running main thread, and an id no thread
 * has is never a thread.  THREAD_QUERY_INFORMATION is passed as the access mask; the runtime
 * does not check it. */
#define THREAD_QUERY_INFORMATION 0x0040

TEST(kernel32, OpenThread)
{
    HANDLE h;
    DWORD code;

    T_CASE("opens the running thread with the given id, and success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, OpenThread, THREAD_QUERY_INFORMATION, FALSE, CALL(kernel32, GetCurrentThreadId));
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    code = 0xC0DE;
    T_EQ(CALL(kernel32, GetExitCodeThread, h, &code), TRUE);
    T_EQ(code, STILL_ACTIVE);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("an id no running thread has fails with NULL and ERROR_INVALID_PARAMETER");
    h = CALL(kernel32, OpenThread, THREAD_QUERY_INFORMATION, FALSE, 0x7FFFFFF0);
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
}
