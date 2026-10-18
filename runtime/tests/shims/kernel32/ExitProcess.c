#include "rt/test.h"
#include "rt/test_child.h"

/* ExitProcess ends the process, so every call runs in a forked child.  The checks are made in
 * the parent, from the child's exit status and what it wrote. */
static void exit_with(void *arg)
{
    CALL(kernel32, ExitProcess, *(UINT *)arg);
}

TEST(kernel32, ExitProcess)
{
    RtTestChild c;
    UINT code;

    T_CASE("exits with the given code, 0 and 7");
    code = 0;
    rt_test_child_run(exit_with, &code, &c);
    T_TRUE(T_CHILD_EXITED(&c, 0));
    T_TRUE(T_CHILD_SAID(&c, "process exit: ExitProcess(0x00000000)"));
    code = 7;
    rt_test_child_run(exit_with, &code, &c);
    T_TRUE(T_CHILD_EXITED(&c, 7));
    T_TRUE(T_CHILD_SAID(&c, "process exit: ExitProcess(0x00000007)"));

    T_CASE("a code of 0xC0000409 exits with status 255");
    code = 0xC0000409;
    rt_test_child_run(exit_with, &code, &c);
    T_TRUE(T_CHILD_EXITED(&c, 255));
}
