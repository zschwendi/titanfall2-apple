#include "rt/test.h"
#include "rt/sync.h"

/* Waits go through rt_wait_handle, the wait core that WaitForSingleObject stands behind:
 * there is no WaitForSingleObject shim to call through the guest ABI.  The name "s" 0xE9 in
 * the ANSI code page (1252) is the wide name L"s" U+00E9. */
static const WCHAR e_name[] = { 's', 0x00E9, 0 };

TEST(kernel32, CreateSemaphoreA)
{
    HANDLE h, h2, ev;
    SECURITY_ATTRIBUTES sa;
    char name[301];
    unsigned i;
    LONG prev;

    T_CASE("an unnamed semaphore starts with its initial count: two waits succeed, the third times out");
    h = CALL(kernel32, CreateSemaphoreA, NULL, 2, 2, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);

    T_CASE("the security attributes are ignored, NULL or not");
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    h = CALL(kernel32, CreateSemaphoreA, &sa, 1, 1, NULL);
    T_TRUE(h != NULL);
    T_EQ(sa.nLength, sizeof sa);                    /* the function writes nothing into it */
    T_EQ(sa.bInheritHandle, TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);

    T_CASE("invalid counts fail with ERROR_INVALID_PARAMETER, also when the name exists");
    h = CALL(kernel32, CreateSemaphoreA, NULL, 0, 0, NULL);          /* maximum 0 */
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    h = CALL(kernel32, CreateSemaphoreA, NULL, -1, 1, NULL);         /* initial below 0 */
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    h = CALL(kernel32, CreateSemaphoreA, NULL, 2, 1, NULL);          /* initial above maximum */
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    h = CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, "counts");
    T_REQUIRE(h != NULL);
    h = CALL(kernel32, CreateSemaphoreA, NULL, 2, 1, "counts");
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a named semaphore created twice is one object: ERROR_ALREADY_EXISTS, and the counts are ignored");
    h = CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, "shared");
    T_REQUIRE(h != NULL);
    h2 = CALL(kernel32, CreateSemaphoreA, NULL, 0, 5, "shared");
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(rt_wait_handle(T_CTX(), h2, 0, FALSE), WAIT_OBJECT_0);     /* the first count, 1: the 0 is ignored */
    T_EQ(rt_wait_handle(T_CTX(), h2, 0, FALSE), WAIT_TIMEOUT);
    T_TRUE(rt_semaphore_release(T_CTX(), h2, 1, &prev));            /* count 1 */
    T_EQ(rt_semaphore_release(T_CTX(), h2, 1, &prev), FALSE);       /* the first maximum, 1: the 5 is ignored */

    T_CASE("the name is in the ANSI code page: byte 0xE9 is the wide name U+00E9");
    h = CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, "s\xE9");
    T_REQUIRE(h != NULL);
    h2 = rt_semaphore_create(T_CTX(), 1, 1, e_name);
    T_TRUE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);

    T_CASE("a name of MAX_PATH characters is accepted");
    for (i = 0; i < 259; i++) name[i] = 'n';
    name[259] = 0;
    h = CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, name);      /* 259 characters */
    T_TRUE(h != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    name[259] = 'n';
    name[260] = 0;
    h = CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, name);      /* MAX_PATH: 260 characters */
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(CALL(kernel32, CloseHandle, h), TRUE);

    T_CASE("a name longer than MAX_PATH fails with ERROR_FILENAME_EXCED_RANGE");
    for (i = 259; i < 261; i++) name[i] = 'n';
    name[261] = 0;
    h = CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, name);      /* MAX_PATH + 1: 261 characters */
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILENAME_EXCED_RANGE);
    for (i = 261; i < 300; i++) name[i] = 'n';
    name[300] = 0;
    h = CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, name);      /* 300 characters */
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILENAME_EXCED_RANGE);

    T_CASE("a name held by an event fails with ERROR_INVALID_HANDLE");
    ev = CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, "taken");
    T_REQUIRE(ev != NULL);
    h = CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, "taken");
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("a new object sets last error to ERROR_SUCCESS");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, "fresh");
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    /* The empty name and a backslash are refused by the core (RT-TRAPS): each stops the run in the
     * handle unit, and the report names the case. The shim does not check them. */
    T_TRAPS(CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, ""), "empty object name");
    T_TRAPS(CALL(kernel32, CreateSemaphoreA, NULL, 1, 1, "a\\b"), "backslash");
}
