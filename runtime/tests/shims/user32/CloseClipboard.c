#include "rt/test.h"
#include "rt/window.h"

/* The clipboard starts closed in each test process. */
TEST(user32, CloseClipboard)
{
    T_CASE("fails with ERROR_CLIPBOARD_NOT_OPEN while the clipboard is closed");
    T_EQ(CALL(user32, CloseClipboard), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);

    T_CASE("one close closes the clipboard that was opened twice");
    T_EQ(CALL(user32, OpenClipboard, NULL), TRUE);
    T_EQ(CALL(user32, OpenClipboard, NULL), TRUE);
    T_EQ(CALL(user32, CloseClipboard), TRUE);

    T_CASE("a second close fails with ERROR_CLIPBOARD_NOT_OPEN");
    T_EQ(CALL(user32, CloseClipboard), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);

    T_CASE("does not change last error on success");
    T_EQ(CALL(user32, OpenClipboard, NULL), TRUE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, CloseClipboard), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
