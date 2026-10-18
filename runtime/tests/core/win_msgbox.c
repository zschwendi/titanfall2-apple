/* rt_message_box (rt/window.h): the caption and the text reach the log and the fatal report,
 * and the run ends.  The child's output is the log and the report together (rt/test_child.h). */
#include "rt/test.h"
#include "rt/test_child.h"
#include "rt/window.h"

typedef struct { RtText text, caption; UINT type; HWND owner; } Box;
static void show(void *arg) { Box *b = arg; rt_message_box(T_CTX(), b->owner, b->text, b->caption, b->type); }

TEST(core, win_msgbox)
{
    static RtTestChild c;

    T_CASE("narrow text: every line is logged, then the report, then abort");
    Box b = { RT_TEXT_A("Engine Error\r\nCould not load \x93gfx\x94: caf\xe9 \x01 \x81\nthird\n"),
              RT_TEXT_A("Titanfall 2"), 0x10, (HWND)(uintptr_t)0x40000404u };
    rt_test_child_run(show, &b, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "trap: MessageBox caption: Titanfall 2\n"));
    T_TRUE(T_CHILD_SAID(&c, "trap: MessageBox text: Engine Error\n"));
    T_TRUE(T_CHILD_SAID(&c, "trap: MessageBox text: Could not load \xe2\x80\x9cgfx\xe2\x80\x9d: caf\xc3\xa9 \\x01 \\x81\n"));
    T_TRUE(T_CHILD_SAID(&c, "trap: MessageBox text: third\n"));
    T_EQ(rt_test_count(c.out, "MessageBox text:"), 3);          /* no empty line after the last "\n" */
    T_TRUE(T_CHILD_SAID(&c, "FATAL guest called MessageBox: Titanfall 2\n  text: Engine Error\n    Could not load "));
    T_TRUE(T_CHILD_SAID(&c, "\n    third\n"));
    T_TRUE(T_CHILD_SAID(&c, "  uType 0x10, owner window 0x40000404\n"));
    T_EQ(rt_test_count(c.out, "FATAL"), 1);
    T_TRUE(strstr(c.out, "MessageBox caption") < strstr(c.out, "FATAL"));   /* the log comes first */

    T_CASE("wide text: UTF-16 is written as UTF-8");
    static const WCHAR wt[] = { 'a', 0x20AC, 0xD83D, 0xDE00, 'b', 0xDC00, '\t', 'c', 0 };
    b = (Box){ RT_TEXT_W(wt), RT_TEXT_W(u"Wide \x00e9"), 0, NULL };
    rt_test_child_run(show, &b, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "MessageBox caption: Wide \xc3\xa9\n"));
    T_TRUE(T_CHILD_SAID(&c, "MessageBox text: a\xe2\x82\xac\xf0\x9f\x98\x80" "b\\udc00\tc\n"));
    T_TRUE(T_CHILD_SAID(&c, "  uType 0x0, owner window 0x0\n"));

    T_CASE("NULL text and NULL caption are said, not replaced");
    b = (Box){ RT_TEXT_A(NULL), RT_TEXT_W(NULL), 0x30, NULL };
    rt_test_child_run(show, &b, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "MessageBox caption: (null)\n"));
    T_TRUE(T_CHILD_SAID(&c, "MessageBox text: (null)\n"));
    T_TRUE(T_CHILD_SAID(&c, "FATAL guest called MessageBox: (null)\n  text: (null)\n  uType 0x30"));

    T_CASE("a long text is logged whole and cut in the report");
    static char big[6001];
    for (unsigned i = 0; i < 6000; i++) big[i] = (i % 100 == 99) ? '\n' : (char)('A' + i / 100 % 26);
    b = (Box){ RT_TEXT_A(big), RT_TEXT_A("Big"), 0, NULL };
    rt_test_child_run(show, &b, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_EQ(rt_test_count(c.out, "MessageBox text: "), 60);
    T_TRUE(T_CHILD_SAID(&c, "(4000 more bytes are in the log)"));

    T_CASE("the text is data, never a format");
    b = (Box){ RT_TEXT_A("100%s done %n"), RT_TEXT_A("%d%%"), 0, NULL };
    rt_test_child_run(show, &b, &c);
    T_TRUE(T_CHILD_SAID(&c, "MessageBox caption: %d%%\n"));
    T_TRUE(T_CHILD_SAID(&c, "MessageBox text: 100%s done %n\n"));
}
