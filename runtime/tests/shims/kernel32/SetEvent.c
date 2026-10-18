#include "rt/test.h"
#include "rt/handle.h"
#include "rt/sync.h"

/* Events are made with the core's own calls (rt_event_create), and their state is read with
 * rt_wait_handle at timeout 0: a wait on a set event returns WAIT_OBJECT_0 (an auto-reset event
 * is consumed by it, a manual-reset event is not), and a wait on a reset event returns
 * WAIT_TIMEOUT.  No thread is started.  The other-kind object is a file-kind object, made as in
 * tests/shims/kernel32/CloseHandle.c. */
static void nop_destroy(RtObject *o)
{
    (void)o;
}

static const RtObjectOps other_ops = { .name = "test-other", .destroy = nop_destroy };

static HANDLE make_other(void)
{
    RtObject *o = rt_object_new(RT_K_FILE, sizeof(RtObject), &other_ops, NULL, NULL);
    T_TRUE(o != NULL);
    return o ? rt_handle_new(o) : NULL;
}

TEST(kernel32, SetEvent)
{
    HANDLE h;

    T_CASE("a manual-reset event stays set after SetEvent");
    h = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);
    T_EQ(CALL(kernel32, SetEvent, h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);  /* a wait does not consume it */

    T_CASE("setting an event that is already set changes nothing");
    h = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, SetEvent, h), TRUE);
    T_EQ(CALL(kernel32, SetEvent, h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    h = rt_event_create(T_CTX(), FALSE, FALSE, NULL);           /* auto-reset: two sets are one */
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, SetEvent, h), TRUE);
    T_EQ(CALL(kernel32, SetEvent, h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);

    T_CASE("an auto-reset event stays set until one wait consumes it");
    h = rt_event_create(T_CTX(), FALSE, FALSE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, SetEvent, h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);  /* the wait consumes it */
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);

    T_CASE("NULL, a closed handle and a handle of another kind fail with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, SetEvent, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetEvent, (HANDLE)(uintptr_t)4), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    h = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(CALL(kernel32, SetEvent, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    h = make_other();
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, SetEvent, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("does not change last error on success");
    h = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
    T_REQUIRE(h != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetEvent, h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
