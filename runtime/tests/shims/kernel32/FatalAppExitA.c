#include "rt/test.h"
#include "rt/test_child.h"

/* FatalAppExitA ends the process with status 3 after its report, so every call runs in a
 * forked child.  The checks are made in the parent, from the child's exit status and what it
 * wrote to descriptor 2. */
typedef struct {
    UINT        action;
    LPCSTR      message;
} FatalArgs;

static void fatal_exit(void *arg)
{
    FatalArgs *a = arg;
    CALL(kernel32, FatalAppExitA, a->action, a->message);
}

TEST(kernel32, FatalAppExitA)
{
    RtTestChild c;
    FatalArgs a;

    T_CASE("prints the message and ends the process with status 3");
    a.action = 0;
    a.message = "game over: code 42";
    rt_test_child_run(fatal_exit, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 3));
    T_TRUE(T_CHILD_SAID(&c, "FATAL guest called FatalAppExit: game over: code 42"));
    T_TRUE(T_CHILD_SAID(&c, "uAction 0x"));

    T_CASE("a NULL message prints (null)");
    a.action = 0;
    a.message = NULL;
    rt_test_child_run(fatal_exit, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 3));
    T_TRUE(T_CHILD_SAID(&c, "FATAL guest called FatalAppExit: (null)"));
}
