#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/window.h"

/* A module handle for the registrations and the unregistrations.  The core does not compare
 * it (one class per name), so the same value is used throughout. */
#define INST ((HINSTANCE)0x140000000ull)
/* A guest procedure address; no class of these tests is ever called. */
#define PROC ((WNDPROC)0x140001000ull)

/* Registers, through the core, a class named `name` (code page 1252).  Returns its atom. */
static ATOM reg_a(const char *name)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = PROC;
    wc.hInstance = INST;
    wc.lpszClassName = (LPCWSTR)name;           /* unicode false: the member is a char string */
    return rt_wndclass_register(&wc, false);
}

/* Not checked here: ERROR_CLASS_HAS_WINDOWS.  It needs a window of the class, and no window
 * can be created yet (rt_window_create is not built: core/win_window.c). */
TEST(user32, UnregisterClassA)
{
    char longname[257];
    ATOM atom;

    T_CASE("unregisters a class by its name");
    T_TRUE(reg_a("Plain") != 0);
    T_EQ(CALL(user32, UnregisterClassA, "Plain", INST), TRUE);

    T_CASE("the name is compared without regard to case");
    T_TRUE(reg_a("Mixed") != 0);
    T_EQ(CALL(user32, UnregisterClassA, "mIXED", INST), TRUE);

    T_CASE("unregisters a class by its atom");
    atom = reg_a("ByAtom");
    T_TRUE(atom >= 0xC000);
    T_EQ(CALL(user32, UnregisterClassA, (LPCVOID)(uintptr_t)atom, INST), TRUE);

    T_CASE("a second unregistration fails with ERROR_CLASS_DOES_NOT_EXIST");
    T_EQ(CALL(user32, UnregisterClassA, "Plain", INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);
    T_EQ(CALL(user32, UnregisterClassA, (LPCVOID)(uintptr_t)atom, INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);

    T_CASE("a NULL name fails with ERROR_CLASS_DOES_NOT_EXIST");
    T_EQ(CALL(user32, UnregisterClassA, NULL, INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);

    T_CASE("an empty name fails with ERROR_CLASS_DOES_NOT_EXIST");
    T_EQ(CALL(user32, UnregisterClassA, "", INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);

    T_CASE("a name longer than 255 characters fails with ERROR_CLASS_DOES_NOT_EXIST");
    memset(longname, 'x', 255);
    longname[255] = 0;                          /* 255 characters: the longest class name */
    T_TRUE(reg_a(longname) != 0);
    longname[255] = 'x';
    longname[256] = 0;                          /* 256 characters: no class has that name */
    T_EQ(CALL(user32, UnregisterClassA, longname, INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);
    longname[255] = 0;
    T_EQ(CALL(user32, UnregisterClassA, longname, INST), TRUE);

    T_CASE("success does not change last error");
    T_TRUE(reg_a("Quiet") != 0);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, UnregisterClassA, "Quiet", INST), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
