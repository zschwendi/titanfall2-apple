#include "rt/test.h"
#include "rt/test_child.h"

static void box(void *arg) { (void)arg; CALL(user32, MessageBoxA, NULL, "Disk full\nRetry?", "Engine Error", 0x15); }

TEST(user32, MessageBoxA)
{
    /* The run ends in a report that carries the caption and the text. */

    /* Before the report, the text is in the log, one line for each of its lines. */
    T_CASE("the caption and each line of the text are logged before the run ends");
    static RtTestChild c;
    rt_test_child_run(box, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "trap: MessageBox caption: Engine Error\n"));
    T_TRUE(T_CHILD_SAID(&c, "trap: MessageBox text: Disk full\n"));
    T_TRUE(T_CHILD_SAID(&c, "trap: MessageBox text: Retry?\n"));
    T_TRUE(T_CHILD_SAID(&c, "in shim user32.dll!MessageBoxA") || T_CHILD_SAID(&c, "user32.dll!MessageBoxA"));

    /* RT-TRAPS: the run ends in a report that carries the caption and the text. */
    T_TRAPS(CALL(user32, MessageBoxA, NULL, "Could not load library client", "Engine Error", 0x10),
            "FATAL guest called MessageBox: Engine Error\n  text: Could not load library client\n  uType 0x10");
    T_TRAPS(CALL(user32, MessageBoxA, NULL, NULL, NULL, 0), "FATAL guest called MessageBox: (null)");
}
