#include "rt/test.h"
#include "rt/window.h"

TEST(user32, GetClipboardData)
{
    T_CASE("NULL with ERROR_NOT_FOUND for a format that is not set");
    T_EQ(rt_clipboard_open(NULL), TRUE);
    T_EQ(CALL(user32, GetClipboardData, CF_UNICODETEXT), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_FOUND);

    T_CASE("the handle set for a format while the clipboard is open");
    T_EQ(rt_clipboard_set(0xC123, (HANDLE)0x5000), (HANDLE)0x5000);
    T_EQ(CALL(user32, GetClipboardData, 0xC123), (HANDLE)0x5000);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetClipboardData, 0xC123), (HANDLE)0x5000);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("NULL with ERROR_CLIPBOARD_NOT_OPEN when the clipboard is not open");
    T_EQ(rt_clipboard_close(), TRUE);
    T_EQ(CALL(user32, GetClipboardData, 0xC123), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);

    /* The core's stop, after the checks of the last case: a text format that is not set while
     * another of the three text formats is. */
    T_EQ(rt_clipboard_open(NULL), TRUE);
    T_EQ(rt_clipboard_set(CF_UNICODETEXT, (HANDLE)0x5001), (HANDLE)0x5001);
    T_TRAPS(CALL(user32, GetClipboardData, CF_TEXT), "rt_clipboard_get: not implemented");
}
