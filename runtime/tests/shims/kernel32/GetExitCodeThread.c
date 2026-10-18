#include "rt/test.h"
#include "rt/handle.h"
#include "rt/thread.h"

/* The thread handle comes from the core (rt_thread_open) and the event from the core's object
 * calls, as in runtime/tests/shims/kernel32/CloseHandle.c.  No thread is started: the core's
 * tests cover a thread that has ended. */
static void no_destroy(RtObject *o)
{
    (void)o;
}

static const RtObjectOps event_ops = { .name = "test-event", .destroy = no_destroy };

TEST(kernel32, GetExitCodeThread)
{
    HANDLE th, ev;
    DWORD code;

    T_CASE("a running thread gives STILL_ACTIVE and TRUE, and so does the pseudo-handle of the current thread");
    th = rt_thread_open(CALL(kernel32, GetCurrentThreadId));
    T_REQUIRE(th != NULL);
    code = 0xC0DE;
    T_EQ(CALL(kernel32, GetExitCodeThread, th, &code), TRUE);
    T_EQ(code, STILL_ACTIVE);
    code = 0xC0DE;
    T_EQ(CALL(kernel32, GetExitCodeThread, RT_PSEUDO_THREAD, &code), TRUE);
    T_EQ(code, STILL_ACTIVE);
    T_EQ(rt_handle_close(th), TRUE);

    T_CASE("NULL and a handle that is not a thread fail with ERROR_INVALID_HANDLE and leave the DWORD unwritten");
    code = 0xC0DE;
    T_EQ(CALL(kernel32, GetExitCodeThread, NULL, &code), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(code, 0xC0DE);
    ev = rt_handle_new(rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL));
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(kernel32, GetExitCodeThread, ev, &code), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(code, 0xC0DE);
    T_EQ(rt_handle_close(ev), TRUE);

    T_CASE("success does not change last error");
    th = rt_thread_open(CALL(kernel32, GetCurrentThreadId));
    T_REQUIRE(th != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    code = 0xC0DE;
    T_EQ(CALL(kernel32, GetExitCodeThread, th, &code), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetExitCodeThread, RT_PSEUDO_THREAD, &code), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_handle_close(th), TRUE);

    /* The stop: after the checks of the last case, it gets no case of its own. */
    T_TRAPS(CALL(kernel32, GetExitCodeThread, RT_PSEUDO_THREAD, NULL), "lpExitCode is NULL");
}
