#include "rt/test.h"
#include "rt/sync.h"

/* Mutexes are made with CreateMutexA through the guest, and the depth the test needs is
 * taken with the core's rt_wait_handle (there is no WaitForSingleObject shim in this test).
 * Only one thread is used: a release by a thread that does not own the mutex, because another
 * thread owns it, is not tested here (shim tests start no threads); the core covers it. */

TEST(kernel32, ReleaseMutex)
{
    HANDLE h, e;

    T_CASE("releases an owned mutex, once for each time it was owned");
    h = CALL(kernel32, CreateMutexA, NULL, TRUE, NULL);      /* depth 1 */
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0); /* depth 2 */
    T_EQ(CALL(kernel32, ReleaseMutex, h), TRUE);
    T_EQ(CALL(kernel32, ReleaseMutex, h), TRUE);
    T_EQ(CALL(kernel32, ReleaseMutex, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);

    T_CASE("a release of an unowned mutex fails with ERROR_NOT_OWNER and changes nothing");
    h = CALL(kernel32, CreateMutexA, NULL, FALSE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, ReleaseMutex, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);  /* still unowned, so the wait takes it */
    T_EQ(CALL(kernel32, ReleaseMutex, h), TRUE);
    T_EQ(CALL(kernel32, ReleaseMutex, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);

    T_CASE("NULL, a handle that is not open, and a handle of another kind fail with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, ReleaseMutex, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, ReleaseMutex, (HANDLE)(uintptr_t)4), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    e = CALL(kernel32, CreateEventA, NULL, TRUE, TRUE, NULL);
    T_REQUIRE(e != NULL);
    T_EQ(CALL(kernel32, ReleaseMutex, e), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    h = CALL(kernel32, CreateMutexA, NULL, TRUE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, CloseHandle, h), TRUE);
    T_EQ(CALL(kernel32, ReleaseMutex, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("does not change last error on success");
    h = CALL(kernel32, CreateMutexA, NULL, TRUE, NULL);
    T_REQUIRE(h != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, ReleaseMutex, h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
