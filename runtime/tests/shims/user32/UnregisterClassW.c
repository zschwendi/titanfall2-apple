#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/window.h"

/* A module handle for the registrations and the unregistrations.  The core does not compare
 * it (one class per name), so the same value is used throughout. */
#define INST ((HINSTANCE)0x140000000ull)
/* A guest procedure address; no class of these tests is ever called. */
#define PROC ((WNDPROC)0x140001000ull)

/* Registers, through the core, a class named `name` (UTF-16).  Returns its atom. */
static ATOM reg_w(const WCHAR *name)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = PROC;
    wc.hInstance = INST;
    wc.lpszClassName = name;
    return rt_wndclass_register(&wc, true);
}

/* Copies the ASCII string `s` into `d` as UTF-16 (the test's names are all ASCII). */
static void to_w(WCHAR *d, const char *s)
{
    do { *d++ = (WCHAR)(unsigned char)*s; } while (*s++);
}

/* Not checked here: ERROR_CLASS_HAS_WINDOWS.  It needs a window of the class, and no window
 * can be created yet (rt_window_create is not built: core/win_window.c). */
TEST(user32, UnregisterClassW)
{
    WCHAR name[32], longname[257];
    ATOM atom;

    T_CASE("unregisters a class by its name");
    to_w(name, "Plain");
    T_TRUE(reg_w(name) != 0);
    T_EQ(CALL(user32, UnregisterClassW, name, INST), TRUE);

    T_CASE("the name is compared without regard to case");
    to_w(name, "Mixed");
    T_TRUE(reg_w(name) != 0);
    to_w(name, "mIXED");
    T_EQ(CALL(user32, UnregisterClassW, name, INST), TRUE);

    T_CASE("unregisters a class by its atom");
    to_w(name, "ByAtom");
    atom = reg_w(name);
    T_TRUE(atom >= 0xC000);
    T_EQ(CALL(user32, UnregisterClassW, (LPCVOID)(uintptr_t)atom, INST), TRUE);

    T_CASE("a second unregistration fails with ERROR_CLASS_DOES_NOT_EXIST");
    to_w(name, "Plain");
    T_EQ(CALL(user32, UnregisterClassW, name, INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);
    T_EQ(CALL(user32, UnregisterClassW, (LPCVOID)(uintptr_t)atom, INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);

    T_CASE("a NULL name fails with ERROR_CLASS_DOES_NOT_EXIST");
    T_EQ(CALL(user32, UnregisterClassW, NULL, INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);

    T_CASE("an empty name fails with ERROR_CLASS_DOES_NOT_EXIST");
    to_w(name, "");
    T_EQ(CALL(user32, UnregisterClassW, name, INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);

    T_CASE("a name longer than 255 characters fails with ERROR_CLASS_DOES_NOT_EXIST");
    for (unsigned i = 0; i < 255; i++) longname[i] = 'x';
    longname[255] = 0;                          /* 255 characters: the longest class name */
    T_TRUE(reg_w(longname) != 0);
    longname[255] = 'x';
    longname[256] = 0;                          /* 256 characters: no class has that name */
    T_EQ(CALL(user32, UnregisterClassW, longname, INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);
    longname[255] = 0;
    T_EQ(CALL(user32, UnregisterClassW, longname, INST), TRUE);

    T_CASE("success does not change last error");
    to_w(name, "Quiet");
    T_TRUE(reg_w(name) != 0);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, UnregisterClassW, name, INST), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
