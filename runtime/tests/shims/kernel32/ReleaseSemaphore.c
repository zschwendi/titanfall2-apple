#include "rt/test.h"
#include "rt/sync.h"

/* Semaphores and events are made with the core's own calls (rt_semaphore_create,
 * rt_event_create), as in runtime/tests/shims/kernel32/CloseHandle.c.  The count is read by
 * waiting on the semaphore with a zero timeout (rt_wait_handle): each WAIT_OBJECT_0 takes one
 * unit, so drain() returns the count and leaves the semaphore at zero. */
#define PREV_SENTINEL 0x7777
#define GUARD_SENTINEL 0x5A5A5A5A

static HANDLE make_semaphore(LONG initial, LONG maximum)
{
    HANDLE h = rt_semaphore_create(T_CTX(), initial, maximum, NULL);
    T_TRUE(h != NULL);
    return h;
}

static LONG drain(HANDLE h)
{
    LONG n = 0;
    while (n < 100 && rt_wait_handle(T_CTX(), h, 0, FALSE) == WAIT_OBJECT_0)
        n++;
    return n;
}

TEST(kernel32, ReleaseSemaphore)
{
    /* Two LONGs: the previous count is written into `prev`, and `guard` must stay as it was,
     * so that the function writes exactly one LONG. */
    struct { LONG prev; LONG guard; } box;
    HANDLE h;

    T_CASE("release raises the count and stores the previous count");
    h = make_semaphore(1, 5);
    box.prev = PREV_SENTINEL;
    box.guard = GUARD_SENTINEL;
    T_EQ(CALL(kernel32, ReleaseSemaphore, h, 2, &box.prev), TRUE);
    T_EQ(box.prev, 1);
    T_EQ(box.guard, GUARD_SENTINEL);
    T_EQ(drain(h), 3);

    T_CASE("NULL previous count is accepted and the count is still raised");
    h = make_semaphore(0, 3);
    T_EQ(CALL(kernel32, ReleaseSemaphore, h, 2, NULL), TRUE);
    T_EQ(drain(h), 2);

    T_CASE("release count of 0 or less fails with ERROR_INVALID_PARAMETER and changes nothing");
    h = make_semaphore(1, 5);
    box.prev = PREV_SENTINEL;
    T_EQ(CALL(kernel32, ReleaseSemaphore, h, 0, &box.prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(box.prev, PREV_SENTINEL);
    T_EQ(CALL(kernel32, ReleaseSemaphore, h, -1, &box.prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(box.prev, PREV_SENTINEL);
    T_EQ(drain(h), 1);

    T_CASE("passing the maximum fails with ERROR_TOO_MANY_POSTS, count unchanged, previous count not written");
    h = make_semaphore(2, 3);
    box.prev = PREV_SENTINEL;
    T_EQ(CALL(kernel32, ReleaseSemaphore, h, 1, &box.prev), TRUE);   /* reaching the maximum is fine */
    T_EQ(box.prev, 2);
    box.prev = PREV_SENTINEL;
    T_EQ(CALL(kernel32, ReleaseSemaphore, h, 1, &box.prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(box.prev, PREV_SENTINEL);
    T_EQ(CALL(kernel32, ReleaseSemaphore, h, 0x7FFFFFFF, &box.prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(box.prev, PREV_SENTINEL);
    T_EQ(drain(h), 3);

    T_CASE("a handle that is not a semaphore fails with ERROR_INVALID_HANDLE");
    h = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
    T_REQUIRE(h != NULL);
    box.prev = PREV_SENTINEL;
    T_EQ(CALL(kernel32, ReleaseSemaphore, h, 1, &box.prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(box.prev, PREV_SENTINEL);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);   /* the event is still reset */

    T_CASE("NULL and a handle that is not open fail with ERROR_INVALID_HANDLE");
    box.prev = PREV_SENTINEL;
    T_EQ(CALL(kernel32, ReleaseSemaphore, NULL, 1, &box.prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(box.prev, PREV_SENTINEL);
    h = make_semaphore(0, 3);
    T_EQ(CALL(kernel32, CloseHandle, h), TRUE);
    T_EQ(CALL(kernel32, ReleaseSemaphore, h, 1, &box.prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(box.prev, PREV_SENTINEL);

    T_CASE("does not change last error on success");
    h = make_semaphore(0, 3);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, ReleaseSemaphore, h, 1, &box.prev), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(box.prev, 0);
}
