#include "rt/test.h"
#include "rt/handle.h"
#include "rt/thread.h"
#include "rt/win/process.h"

/* The class is read back through the core (rt_process_get_priority_class), not through the
 * GetPriorityClass shim.  SetPriorityClass writes no guest memory, so the checks are the return
 * value, last error and the class the core records.  The event object is made with the core's
 * own calls, as in runtime/tests/shims/kernel32/CloseHandle.c. */
static void nop_destroy(RtObject *o)
{
    (void)o;
}

static const RtObjectOps event_ops = { .name = "test-event", .destroy = nop_destroy };

static HANDLE make_event(void)
{
    RtObject *o = rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL);
    T_TRUE(o != NULL);
    return o ? rt_handle_new(o) : NULL;
}

TEST(kernel32, SetPriorityClass)
{
    static const DWORD classes[] = {
        NORMAL_PRIORITY_CLASS, IDLE_PRIORITY_CLASS, HIGH_PRIORITY_CLASS,
        REALTIME_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS,
    };
    HANDLE ev;
    unsigned i;

    T_CASE("sets each of the six priority classes");
    T_EQ(rt_process_get_priority_class((HANDLE)-1), NORMAL_PRIORITY_CLASS);  /* the starting class */
    for (i = 0; i < sizeof classes / sizeof classes[0]; i++) {
        T_EQ(CALL(kernel32, SetPriorityClass, (HANDLE)-1, classes[i]), TRUE);
        T_EQ(rt_process_get_priority_class((HANDLE)-1), classes[i]);
    }

    T_CASE("a value that is no priority class fails with ERROR_INVALID_PARAMETER and leaves the class unchanged");
    T_EQ(CALL(kernel32, SetPriorityClass, (HANDLE)-1, HIGH_PRIORITY_CLASS), TRUE);
    T_EQ(CALL(kernel32, SetPriorityClass, (HANDLE)-1, 0x1234), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(rt_process_get_priority_class((HANDLE)-1), HIGH_PRIORITY_CLASS);

    T_CASE("NULL and an event handle fail with ERROR_INVALID_HANDLE and leave the class unchanged");
    ev = make_event();
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(kernel32, SetPriorityClass, NULL, IDLE_PRIORITY_CLASS), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetPriorityClass, ev, IDLE_PRIORITY_CLASS), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_process_get_priority_class((HANDLE)-1), HIGH_PRIORITY_CLASS);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetPriorityClass, (HANDLE)-1, NORMAL_PRIORITY_CLASS), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_process_get_priority_class((HANDLE)-1), NORMAL_PRIORITY_CLASS);

    /* The core stops (fatal) on the two background-mode values, for a valid process handle.
     * It prints the same line for both. */
    T_TRAPS(CALL(kernel32, SetPriorityClass, (HANDLE)-1, PROCESS_MODE_BACKGROUND_BEGIN),
            "SetPriorityClass: PROCESS_MODE_BACKGROUND_BEGIN and _END are not handled");
    T_TRAPS(CALL(kernel32, SetPriorityClass, (HANDLE)-1, PROCESS_MODE_BACKGROUND_END),
            "SetPriorityClass: PROCESS_MODE_BACKGROUND_BEGIN and _END are not handled");

    T_EQ(CALL(kernel32, SetPriorityClass, (HANDLE)-1, NORMAL_PRIORITY_CLASS), TRUE);
    T_EQ(rt_process_get_priority_class((HANDLE)-1), NORMAL_PRIORITY_CLASS);
}
