#include "rt/test.h"
#include "rt/window.h"

TEST(user32, SetClipboardData)
{
    T_CASE("returns the handle and keeps it for the format while the clipboard is open");
    T_EQ(rt_clipboard_open(NULL), TRUE);
    T_EQ(CALL(user32, SetClipboardData, 0xC123, (HANDLE)0x5000), (HANDLE)0x5000);
    T_EQ(CALL(user32, GetClipboardData, 0xC123), (HANDLE)0x5000);

    T_CASE("setting a format that is set replaces its handle");
    T_EQ(CALL(user32, SetClipboardData, 0xC123, (HANDLE)0x6000), (HANDLE)0x6000);
    T_EQ(CALL(user32, GetClipboardData, 0xC123), (HANDLE)0x6000);

    T_CASE("NULL with ERROR_CLIPBOARD_NOT_OPEN for format 0 although the clipboard is open");
    T_EQ(CALL(user32, SetClipboardData, 0, (HANDLE)0x5001), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetClipboardData, 0xC124, (HANDLE)0x5002), (HANDLE)0x5002);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("NULL with ERROR_CLIPBOARD_NOT_OPEN when the clipboard is not open");
    T_EQ(rt_clipboard_close(), TRUE);
    T_EQ(CALL(user32, SetClipboardData, 0xC125, (HANDLE)0x5003), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);
    /* the failed set stored nothing */
    T_EQ(rt_clipboard_open(NULL), TRUE);
    T_EQ(CALL(user32, GetClipboardData, 0xC125), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_FOUND);

    /* The core's stops, after the checks of the last case.  Each one is a fatal report in a
     * forked child, so the parent goes on.  The count of formats starts from an empty clipboard. */
    T_TRAPS(CALL(user32, SetClipboardData, CF_UNICODETEXT, NULL), "rt_clipboard_set: not implemented");
    T_EQ(rt_clipboard_empty(), TRUE);
    for (uint64_t f = 0xC100; f < 0xC110; f++)
        T_EQ(CALL(user32, SetClipboardData, (UINT)f, (HANDLE)(f + 0x1000)), (HANDLE)(f + 0x1000));
    T_TRAPS(CALL(user32, SetClipboardData, 0xC110, (HANDLE)0x5110), "rt_clipboard_set: not implemented");
}
