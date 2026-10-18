#include "rt/test.h"
#include "rt/handle.h"
#include "rt/sync.h"

/* Events are made with the core's own calls (rt_event_create) and set with rt_event_set, so
 * this test does not depend on the SetEvent shim.  The state is read with rt_wait_handle at
 * timeout 0: WAIT_OBJECT_0 for a set event, WAIT_TIMEOUT for a reset one.  No thread is
 * started, so the "nobody is woken" part of the contract is not observable here.  The
 * other-kind object is a file-kind object, made as in tests/shims/kernel32/CloseHandle.c. */
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

TEST(kernel32, ResetEvent)
{
    HANDLE h;

    T_CASE("set then reset: a later wait times out");
    h = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(rt_event_set(T_CTX(), h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(CALL(kernel32, ResetEvent, h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);

    T_CASE("resetting an event that is already reset changes nothing");
    h = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, ResetEvent, h), TRUE);
    T_EQ(CALL(kernel32, ResetEvent, h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);
    h = rt_event_create(T_CTX(), FALSE, FALSE, NULL);           /* auto-reset */
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, ResetEvent, h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);

    T_CASE("an auto-reset event that is set is made unset");
    h = rt_event_create(T_CTX(), FALSE, TRUE, NULL);            /* created set */
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, ResetEvent, h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);

    T_CASE("NULL, a closed handle and a handle of another kind fail with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, ResetEvent, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, ResetEvent, (HANDLE)(uintptr_t)4), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    h = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(CALL(kernel32, ResetEvent, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    h = make_other();
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, ResetEvent, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("does not change last error on success");
    h = rt_event_create(T_CTX(), TRUE, TRUE, NULL);
    T_REQUIRE(h != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, ResetEvent, h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
