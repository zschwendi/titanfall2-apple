#include "rt/test.h"
#include "rt/sync.h"

/* Ownership is observed with the core's own calls (rt_mutex_release, rt_wait_handle), so this
 * test does not depend on the ReleaseMutex or WaitForSingleObject shims.  The guest-level
 * release is tested in tests/shims/kernel32/ReleaseMutex.c. */

/* Fills buf with n copies of c, then a NUL. */
static void fill_name(char *buf, int n, char c)
{
    int i;

    for (i = 0; i < n; i++) buf[i] = c;
    buf[n] = 0;
}

TEST(kernel32, CreateMutexA)
{
    char name[MAX_PATH + 2];
    char junk[16];
    HANDLE h, h2, e;

    T_CASE("an unnamed mutex created owned is owned by the caller and is released once");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, CreateMutexA, NULL, TRUE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(rt_mutex_release(T_CTX(), h), TRUE);
    T_EQ(rt_mutex_release(T_CTX(), h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);

    T_CASE("created unowned, a wait takes it and waits nest");
    h = CALL(kernel32, CreateMutexA, NULL, FALSE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(rt_mutex_release(T_CTX(), h), FALSE);      /* the creator does not own it */
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);   /* the same thread waits again: nests */
    T_EQ(rt_mutex_release(T_CTX(), h), TRUE);
    T_EQ(rt_mutex_release(T_CTX(), h), TRUE);
    T_EQ(rt_mutex_release(T_CTX(), h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);

    T_CASE("a second create with the same name gives a handle to the same mutex with ERROR_ALREADY_EXISTS, and the second create does not own it");
    h = CALL(kernel32, CreateMutexA, NULL, TRUE, "CreateMutexA-named");
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    h2 = CALL(kernel32, CreateMutexA, NULL, TRUE, "CreateMutexA-named");
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(rt_mutex_release(T_CTX(), h2), TRUE);      /* one release empties it: the second create added no depth */
    T_EQ(rt_mutex_release(T_CTX(), h), FALSE);      /* so the first handle no longer owns it */
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);

    T_CASE("two unnamed creates give two mutexes, each with ERROR_SUCCESS");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, CreateMutexA, NULL, FALSE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    h2 = CALL(kernel32, CreateMutexA, NULL, FALSE, NULL);
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_TRUE(h != h2);

    T_CASE("a name of another kind of object fails with ERROR_INVALID_HANDLE");
    e = CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, "CreateMutexA-event");
    T_REQUIRE(e != NULL);
    h = CALL(kernel32, CreateMutexA, NULL, FALSE, "CreateMutexA-event");
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("a name of MAX_PATH characters is accepted");
    fill_name(name, MAX_PATH - 1, 'a');                 /* one shorter: accepted too */
    h = CALL(kernel32, CreateMutexA, NULL, FALSE, name);
    T_TRUE(h != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(rt_handle_close(h), TRUE);
    fill_name(name, MAX_PATH, 'a');                     /* exactly MAX_PATH */
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, CreateMutexA, NULL, FALSE, name);
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("a name longer than MAX_PATH fails with ERROR_FILENAME_EXCED_RANGE");
    fill_name(name, MAX_PATH + 1, 'b');
    h = CALL(kernel32, CreateMutexA, NULL, FALSE, name);
    T_TRUE(h == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILENAME_EXCED_RANGE);

    T_CASE("the security attributes pointer, NULL or not, has no effect");
    h = CALL(kernel32, CreateMutexA, (LPSECURITY_ATTRIBUTES)(void *)junk, TRUE, "CreateMutexA-attrs");
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(rt_mutex_release(T_CTX(), h), TRUE);       /* still owned by the caller, as with NULL */
    T_TRAPS(CALL(kernel32, CreateMutexA, NULL, FALSE, ""), "empty name");
    T_TRAPS(CALL(kernel32, CreateMutexA, NULL, FALSE, "a\\b"), "backslash");
}
