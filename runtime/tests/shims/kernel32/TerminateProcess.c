#include "rt/test.h"
#include "rt/test_child.h"
#include "rt/sync.h"

/* TerminateProcess((HANDLE)-1, code) ends the process, so it runs in a child.  The child's
 * function does not return: if the call returned, the child would exit with status 97. */
static void child_terminate(void *arg)
{
    UINT code = *(UINT *)arg;
    CALL(kernel32, TerminateProcess, (HANDLE)-1, code);
}

TEST(kernel32, TerminateProcess)
{
    RtTestChild c;
    UINT code;
    HANDLE ev;

    T_CASE("(HANDLE)-1 ends the process with the code as its exit status");
    code = 7;
    rt_test_child_run(child_terminate, &code, &c);
    T_TRUE(T_CHILD_EXITED(&c, 7));
    T_TRUE(T_CHILD_SAID(&c, "process exit: TerminateProcess(0x00000007)"));

    T_CASE("a code above 254 exits with status 255");
    code = 0x100;
    rt_test_child_run(child_terminate, &code, &c);
    T_TRUE(T_CHILD_EXITED(&c, 255));
    T_TRUE(T_CHILD_SAID(&c, "process exit: TerminateProcess(0x00000100)"));

    T_CASE("NULL, a handle that is not open and an event handle fail with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, TerminateProcess, NULL, 7), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, TerminateProcess, (HANDLE)(uintptr_t)4, 7), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    ev = rt_event_create(T_CTX(), FALSE, FALSE, NULL);
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(kernel32, TerminateProcess, ev, 7), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
}
