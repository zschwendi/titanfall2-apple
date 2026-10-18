#include "rt/test.h"
#include "rt/test_child.h"

static void box(void *arg) { (void)arg; CALL(user32, MessageBoxW, NULL, u"Disk full\r\nRetry \x00e9?", u"Engine Error", 0x15); }

TEST(user32, MessageBoxW)
{

    T_CASE("the caption and each line of the text are logged before the run ends");
    static RtTestChild c;
    rt_test_child_run(box, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "trap: MessageBox caption: Engine Error\n"));
    T_TRUE(T_CHILD_SAID(&c, "trap: MessageBox text: Disk full\n"));
    T_TRUE(T_CHILD_SAID(&c, "trap: MessageBox text: Retry \xc3\xa9?\n"));

    /* RT-TRAPS: the run ends in a report that carries the caption and the text. */
    T_TRAPS(CALL(user32, MessageBoxW, NULL, u"Could not load library client", u"Engine Error", 0x10),
            "FATAL guest called MessageBox: Engine Error\n  text: Could not load library client\n  uType 0x10");
    T_TRAPS(CALL(user32, MessageBoxW, NULL, NULL, NULL, 0), "FATAL guest called MessageBox: (null)");
}
