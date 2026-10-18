#include "rt/test.h"
#include "rt/handle.h"

/* Source objects are made with the core's own calls (rt_object_new, rt_handle_new), as in
 * runtime/tests/core/handle.c.  The destroy count shows when the object is really gone. */
static unsigned destroyed;

static void count_destroy(RtObject *o)
{
    (void)o;
    destroyed++;
}

static const RtObjectOps event_ops = { .name = "test-event", .destroy = count_destroy };

static HANDLE make_event(void)
{
    RtObject *o = rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL);
    T_TRUE(o != NULL);
    return o ? rt_handle_new(o) : NULL;
}

TEST(kernel32, DuplicateHandle)
{
    HANDLE src, dup;
    HANDLE sentinel = (HANDLE)(uintptr_t)0x5EA1;
    unsigned n;

    T_CASE("duplicates a handle to the same object, and each handle closes on its own");
    src = make_event();
    T_REQUIRE(src != NULL);
    dup = NULL;
    T_EQ(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, RT_PSEUDO_PROCESS, &dup, 0, FALSE, 0), TRUE);
    T_TRUE(dup != NULL && dup != src);
    n = destroyed;
    T_EQ(CALL(kernel32, CloseHandle, src), TRUE);
    T_EQ(destroyed, n);                             /* the duplicate keeps the object alive */
    T_EQ(CALL(kernel32, CloseHandle, dup), TRUE);
    T_EQ(destroyed, n + 1);                         /* the last handle destroys it */

    T_CASE("DUPLICATE_CLOSE_SOURCE closes the source and keeps the duplicate");
    src = make_event();
    T_REQUIRE(src != NULL);
    dup = NULL;
    n = destroyed;
    T_EQ(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, RT_PSEUDO_PROCESS, &dup, 0, FALSE, DUPLICATE_CLOSE_SOURCE), TRUE);
    T_TRUE(dup != NULL && dup != src);
    T_EQ(CALL(kernel32, CloseHandle, src), FALSE);  /* the source is gone */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(destroyed, n);
    T_EQ(CALL(kernel32, CloseHandle, dup), TRUE);
    T_EQ(destroyed, n + 1);

    T_CASE("a NULL lpTargetHandle duplicates without returning the handle");
    src = make_event();
    T_REQUIRE(src != NULL);
    n = destroyed;
    T_EQ(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, RT_PSEUDO_PROCESS, NULL, 0, FALSE, 0), TRUE);
    T_EQ(CALL(kernel32, CloseHandle, src), TRUE);
    T_EQ(destroyed, n);                             /* the duplicate the caller cannot see still holds it */

    T_CASE("a NULL hTargetProcessHandle with DUPLICATE_CLOSE_SOURCE only closes the source");
    src = make_event();
    T_REQUIRE(src != NULL);
    dup = sentinel;
    n = destroyed;
    T_EQ(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, NULL, &dup, 0x1234, TRUE, DUPLICATE_CLOSE_SOURCE), TRUE);
    T_TRUE(dup == sentinel);                        /* lpTargetHandle is ignored: nothing is written */
    T_EQ(destroyed, n + 1);                         /* no duplicate was made, so the object is gone */
    T_EQ(CALL(kernel32, CloseHandle, src), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("an invalid source handle fails with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, NULL, RT_PSEUDO_PROCESS, &dup, 0, FALSE, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, (HANDLE)(uintptr_t)4, RT_PSEUDO_PROCESS, &dup, 0, FALSE, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, NULL, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* The arguments the shim refuses (RT-TRAPS): each one is a loud stop, checked here under
     * this case because the source is a valid handle for all of them. */
    src = make_event();
    T_REQUIRE(src != NULL);
    T_TRAPS(CALL(kernel32, DuplicateHandle, NULL, src, RT_PSEUDO_PROCESS, &dup, 0, FALSE, 0),
            "hSourceProcessHandle other than the current process");
    T_TRAPS(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, (HANDLE)(uintptr_t)0x1234, &dup, 0, FALSE, 0),
            "hTargetProcessHandle other than the current process");
    T_TRAPS(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, NULL, &dup, 0, FALSE, 0),
            "hTargetProcessHandle NULL without DUPLICATE_CLOSE_SOURCE");
    T_TRAPS(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, RT_PSEUDO_PROCESS, RT_PSEUDO_PROCESS, &dup, 0, FALSE, 0),
            "pseudo-handle as hSourceHandle");
    T_TRAPS(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, RT_PSEUDO_THREAD, RT_PSEUDO_PROCESS, &dup, 0, FALSE, 0),
            "pseudo-handle as hSourceHandle");
    T_TRAPS(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, RT_PSEUDO_PROCESS, &dup, 0, FALSE, 0x4),
            "dwOptions bits other than");
    T_TRAPS(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, RT_PSEUDO_PROCESS, &dup, 0, TRUE, 0),
            "bInheritHandle TRUE");

    T_CASE("dwDesiredAccess is ignored, as access rights are not modelled");
    src = make_event();
    T_REQUIRE(src != NULL);
    dup = NULL;
    T_EQ(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, RT_PSEUDO_PROCESS, &dup, 0x12345678, FALSE, DUPLICATE_SAME_ACCESS), TRUE);
    T_TRUE(dup != NULL && dup != src);
    T_EQ(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, RT_PSEUDO_PROCESS, &dup, 0, FALSE, 0), TRUE);
    T_TRUE(dup != NULL);

    T_CASE("does not change last error on success");
    src = make_event();
    T_REQUIRE(src != NULL);
    dup = NULL;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, DuplicateHandle, RT_PSEUDO_PROCESS, src, RT_PSEUDO_PROCESS, &dup, 0, FALSE, 0), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
