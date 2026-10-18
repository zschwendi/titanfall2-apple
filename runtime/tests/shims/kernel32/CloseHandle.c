#include "rt/test.h"
#include "rt/handle.h"

/* Objects are made with the core's own calls (rt_object_new, rt_handle_new), as in
 * runtime/tests/core/handle.c.  The find-kind object has no_close_handle, so it is closed
 * only by its own close function. */
static unsigned destroyed;

static void count_destroy(RtObject *o)
{
    (void)o;
    destroyed++;
}

static const RtObjectOps event_ops = { .name = "test-event", .destroy = count_destroy };
static const RtObjectOps find_ops = { .name = "test-find", .destroy = count_destroy, .no_close_handle = true };

static HANDLE make_handle(RtKind kind, const RtObjectOps *ops)
{
    RtObject *o = rt_object_new(kind, sizeof(RtObject), ops, NULL, NULL);
    T_TRUE(o != NULL);
    return o ? rt_handle_new(o) : NULL;
}

TEST(kernel32, CloseHandle)
{
    HANDLE h;
    RtObject *o;
    unsigned n;

    T_CASE("closes a handle, so the next use and the next close fail");
    h = make_handle(RT_K_EVENT, &event_ops);
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, CloseHandle, h), TRUE);
    T_EQ(CALL(kernel32, CloseHandle, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRUE(rt_handle_ref(h, RT_K_ANY) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("the object is destroyed with its last reference and not before");
    o = rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL);
    T_REQUIRE(o != NULL);
    rt_object_ref(o);                               /* a second reference, which the test keeps */
    h = rt_handle_new(o);                           /* the slot takes the first one */
    T_REQUIRE(h != NULL);
    n = destroyed;
    T_EQ(CALL(kernel32, CloseHandle, h), TRUE);
    T_EQ(destroyed, n);                             /* the test's reference keeps it alive */
    rt_object_unref(o);
    T_EQ(destroyed, n + 1);

    T_CASE("a pseudo-handle is accepted and has no effect");
    h = make_handle(RT_K_EVENT, &event_ops);
    T_REQUIRE(h != NULL);
    n = destroyed;
    T_EQ(CALL(kernel32, CloseHandle, RT_PSEUDO_PROCESS), TRUE);
    T_EQ(CALL(kernel32, CloseHandle, RT_PSEUDO_THREAD), TRUE);
    T_EQ(destroyed, n);                             /* nothing was closed */
    T_EQ(CALL(kernel32, CloseHandle, h), TRUE);     /* the real handle is still open */

    T_CASE("NULL and a handle that is not open fail with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, CloseHandle, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, CloseHandle, (HANDLE)(uintptr_t)4), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("a find-file handle fails with ERROR_INVALID_HANDLE and stays open");
    h = make_handle(RT_K_FIND, &find_ops);
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, CloseHandle, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_handle_close_kind(h, RT_K_FIND), TRUE); /* its own close function still closes it */

    T_CASE("does not change last error on success");
    h = make_handle(RT_K_EVENT, &event_ops);
    T_REQUIRE(h != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, CloseHandle, h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, CloseHandle, RT_PSEUDO_PROCESS), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
