#include "rt/test.h"
#include "rt/window.h"

/* The clipboard starts closed in each test process.  Owners used: NULL and a handle that is no
 * window.  A window owner cannot be made here: rt_window_create is not built, and the desktop
 * handle RT_HWND_DESKTOP is no window to the core. */
TEST(user32, OpenClipboard)
{
    T_CASE("opens the closed clipboard for a NULL owner");
    T_EQ(CALL(user32, OpenClipboard, NULL), TRUE);

    T_CASE("the same owner opens it again");
    T_EQ(CALL(user32, OpenClipboard, NULL), TRUE);
    T_EQ(CALL(user32, CloseClipboard), TRUE);       /* one close closes: opens are not counted */
    T_EQ(CALL(user32, CloseClipboard), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);

    T_CASE("an owner that is no window fails with ERROR_INVALID_WINDOW_HANDLE");
    T_EQ(CALL(user32, OpenClipboard, (HWND)(uintptr_t)0x0BAD0000), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(CALL(user32, CloseClipboard), FALSE);      /* and the clipboard was not opened */
    T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);
    T_EQ(CALL(user32, OpenClipboard, NULL), TRUE);
    T_EQ(CALL(user32, OpenClipboard, (HWND)(uintptr_t)0x0BAD0000), FALSE);  /* the owner is looked at first */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(CALL(user32, CloseClipboard), TRUE);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, OpenClipboard, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, CloseClipboard), TRUE);
}
