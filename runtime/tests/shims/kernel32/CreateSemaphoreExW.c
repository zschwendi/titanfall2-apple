#include "rt/test.h"
#include "rt/sync.h"

/* Waits go through rt_wait_handle, the wait core that WaitForSingleObject stands behind:
 * there is no WaitForSingleObject shim to call through the guest ABI.
 *
 * A wide literal is not a WCHAR string here: L"" is a 32-bit wchar_t on the host, so the
 * names are u"" (UTF-16) literals, cast to LPCWSTR by WSTR.
 *
 * Every call passes dwFlags 0 and dwDesiredAccess SEMAPHORE_ALL_ACCESS (0x1F0003), except the
 * case that passes access 0 and the flags stop. */
#define WSTR(s) ((LPCWSTR)(s))
#define ACCESS_ALL 0x1F0003
static const WCHAR w_name[] = { 's', 0x00E9, 0 };

TEST(kernel32, CreateSemaphoreExW)
{
    HANDLE h, h2, hn, ev;
    SECURITY_ATTRIBUTES sa;
    LONG prev;
    WCHAR longname[262];
    unsigned i;

    T_CASE("an unnamed semaphore starts with its initial count: two waits succeed, the third times out");
    h = CALL(kernel32, CreateSemaphoreExW, NULL, 2, 2, NULL, 0, ACCESS_ALL);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);
    T_TRUE(rt_handle_close(h));

    T_CASE("the security attributes are ignored, NULL or not");
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    h = CALL(kernel32, CreateSemaphoreExW, &sa, 1, 1, NULL, 0, ACCESS_ALL);
    T_TRUE(h != NULL);
    T_EQ(sa.nLength, sizeof sa);                    /* the function writes nothing into it */
    T_EQ(sa.bInheritHandle, TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_TRUE(rt_handle_close(h));

    T_CASE("the desired access is ignored: access 0 gives a working handle");
    h = CALL(kernel32, CreateSemaphoreExW, NULL, 1, 1, NULL, 0, 0);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_TRUE(rt_handle_close(h));

    T_CASE("invalid counts fail with ERROR_INVALID_PARAMETER, also when the name exists");
    h = CALL(kernel32, CreateSemaphoreExW, NULL, 0, 0, NULL, 0, ACCESS_ALL);          /* maximum 0 */
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    h = CALL(kernel32, CreateSemaphoreExW, NULL, -1, 1, NULL, 0, ACCESS_ALL);         /* initial below 0 */
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    h = CALL(kernel32, CreateSemaphoreExW, NULL, 2, 1, NULL, 0, ACCESS_ALL);          /* initial above maximum */
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    hn = CALL(kernel32, CreateSemaphoreExW, NULL, 1, 1, w_name, 0, ACCESS_ALL);
    T_REQUIRE(hn != NULL);
    h = CALL(kernel32, CreateSemaphoreExW, NULL, 2, 1, w_name, 0, ACCESS_ALL);
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a named semaphore created twice is one object: ERROR_ALREADY_EXISTS, and the counts are ignored");
    h = CALL(kernel32, CreateSemaphoreExW, NULL, 1, 1, WSTR(u"shared"), 0, ACCESS_ALL);
    T_REQUIRE(h != NULL);
    h2 = CALL(kernel32, CreateSemaphoreExW, NULL, 0, 5, WSTR(u"shared"), 0, ACCESS_ALL);
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(rt_wait_handle(T_CTX(), h2, 0, FALSE), WAIT_OBJECT_0);     /* the first count, 1: the 0 is ignored */
    T_EQ(rt_wait_handle(T_CTX(), h2, 0, FALSE), WAIT_TIMEOUT);
    T_TRUE(rt_semaphore_release(T_CTX(), h2, 1, &prev));            /* count 0 to 1 */
    T_EQ(prev, 0);
    T_EQ(rt_semaphore_release(T_CTX(), h2, 1, &prev), FALSE);       /* the first maximum, 1: the 5 is ignored */
    T_TRUE(rt_handle_close(h));
    T_TRUE(rt_handle_close(h2));

    T_CASE("the name is UTF-16 as given: U+00E9 is the same object as the ANSI byte 0xE9");
    h = CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, "s\xE9");      /* the object hn made */
    T_REQUIRE(h != NULL);
    h2 = CALL(kernel32, CreateSemaphoreExW, NULL, 1, 1, w_name, 0, ACCESS_ALL);
    T_TRUE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_TRUE(rt_handle_close(h));
    T_TRUE(rt_handle_close(h2));
    T_TRUE(rt_handle_close(hn));

    T_CASE("a name held by an event fails with ERROR_INVALID_HANDLE");
    ev = CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, "taken");
    T_REQUIRE(ev != NULL);
    h = CALL(kernel32, CreateSemaphoreExW, NULL, 1, 1, WSTR(u"taken"), 0, ACCESS_ALL);
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRUE(rt_handle_close(ev));

    T_CASE("a new object sets last error to ERROR_SUCCESS");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, CreateSemaphoreExW, NULL, 1, 1, WSTR(u"fresh"), 0, ACCESS_ALL);
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_TRUE(rt_handle_close(h));

    T_CASE("the count works: rt_semaphore_release returns the previous count");
    h = CALL(kernel32, CreateSemaphoreExW, NULL, 0, 3, NULL, 0, ACCESS_ALL);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);       /* count 0: not signalled */
    T_TRUE(rt_semaphore_release(T_CTX(), h, 2, &prev));             /* count 0 to 2 */
    T_EQ(prev, 0);
    T_TRUE(rt_semaphore_release(T_CTX(), h, 1, &prev));             /* count 2 to 3 */
    T_EQ(prev, 2);
    prev = -1;
    T_EQ(rt_semaphore_release(T_CTX(), h, 1, &prev), FALSE);        /* count 3 is the maximum */
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(prev, -1);                                                 /* not written on failure */
    T_TRUE(rt_handle_close(h));

    /* The stop for dwFlags and the three names the core refuses (RT-TRAPS): each stops the run,
     * and the report names the case. The shim does not check the names. */
    T_TRAPS(CALL(kernel32, CreateSemaphoreExW, NULL, 1, 1, NULL, 1, ACCESS_ALL), "CreateSemaphoreExW with dwFlags");
    T_TRAPS(CALL(kernel32, CreateSemaphoreExW, NULL, 1, 1, WSTR(u""), 0, ACCESS_ALL), "empty object name");
    T_TRAPS(CALL(kernel32, CreateSemaphoreExW, NULL, 1, 1, WSTR(u"a\\b"), 0, ACCESS_ALL), "backslash");
    for (i = 0; i < 261; i++)
        longname[i] = 'x';
    longname[261] = 0;
    T_TRAPS(CALL(kernel32, CreateSemaphoreExW, NULL, 1, 1, longname, 0, ACCESS_ALL), "code units");
}
