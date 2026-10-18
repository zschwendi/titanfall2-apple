#include "rt/test.h"
#include "rt/window.h"

/* The shim's value is the core's rt_clipboard_empty(), seen through the guest ABI.  The core's
 * getters and close need the clipboard open, so the test opens it to look at what EmptyClipboard
 * left behind.  The clipboard owner is NULL throughout: the window case needs a window record. */
TEST(user32, EmptyClipboard)
{
    HANDLE h1 = (HANDLE)0x1000, h2 = (HANDLE)0x2000;

    T_CASE("not open fails with ERROR_CLIPBOARD_NOT_OPEN and keeps the formats");
    T_EQ(rt_clipboard_open(NULL), TRUE);
    T_EQ(rt_clipboard_set(CF_TEXT, h1), h1);
    T_EQ(rt_clipboard_close(), TRUE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, EmptyClipboard), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);
    T_EQ(rt_clipboard_open(NULL), TRUE);
    T_EQ(rt_clipboard_get(CF_TEXT), h1);
    T_EQ(rt_clipboard_close(), TRUE);

    T_CASE("empties an open clipboard and leaves no format");
    T_EQ(rt_clipboard_open(NULL), TRUE);
    T_EQ(rt_clipboard_set(CF_TEXT, h1), h1);
    T_EQ(rt_clipboard_set(CF_UNICODETEXT, h2), h2);
    T_EQ(CALL(user32, EmptyClipboard), TRUE);
    T_EQ(rt_clipboard_get(CF_TEXT), NULL);
    T_EQ(rt_clipboard_get(CF_UNICODETEXT), NULL);
    T_EQ(CALL(user32, EmptyClipboard), TRUE);     /* empty again: still TRUE */
    T_EQ(rt_clipboard_close(), TRUE);             /* empty leaves the clipboard open, so this succeeds */

    T_CASE("does not change last error");
    T_EQ(rt_clipboard_open(NULL), TRUE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, EmptyClipboard), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_clipboard_close(), TRUE);
}
