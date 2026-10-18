#include "rt/test.h"
#include "rt/win/user.h"


TEST(user32, LoadCursorA)
{
    static const unsigned idc[] = {
        IDC_ARROW, IDC_IBEAM, IDC_WAIT, IDC_CROSS, IDC_UPARROW,
        IDC_SIZENWSE, IDC_SIZENESW, IDC_SIZEWE, IDC_SIZENS, IDC_SIZEALL,
        IDC_NO, IDC_HAND, IDC_APPSTARTING, IDC_HELP,
    };
    unsigned i;

    T_CASE("predefined cursor of every IDC_ identifier, same handle on each call");
    T_EQ(CALL(user32, LoadCursorA, NULL, (LPCVOID)(uintptr_t)IDC_ARROW), (HCURSOR)0x17F00);
    T_EQ(CALL(user32, LoadCursorA, NULL, (LPCVOID)(uintptr_t)IDC_ARROW), (HCURSOR)0x17F00);
    for (i = 0; i < sizeof idc / sizeof idc[0]; i++)
        T_EQ(CALL(user32, LoadCursorA, NULL, (LPCVOID)(uintptr_t)idc[i]),
             (HCURSOR)(uintptr_t)(0x10000u + idc[i]));

    T_CASE("unknown identifier fails with ERROR_RESOURCE_NAME_NOT_FOUND");
    T_EQ(CALL(user32, LoadCursorA, NULL, (LPCVOID)(uintptr_t)100), (HCURSOR)NULL);
    T_EQ(T_LAST_ERROR(), ERROR_RESOURCE_NAME_NOT_FOUND);
    T_EQ(CALL(user32, LoadCursorA, NULL, (LPCVOID)(uintptr_t)32647), (HCURSOR)NULL);   /* between IDC_SIZEALL and IDC_NO */
    T_EQ(T_LAST_ERROR(), ERROR_RESOURCE_NAME_NOT_FOUND);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, LoadCursorA, NULL, (LPCVOID)(uintptr_t)IDC_ARROW);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: a cursor in a module's resources is resource loading (Gate 4), never guessed.
     * The module handle is any non-NULL value: the shim stops before it looks at it. */
    T_TRAPS(CALL(user32, LoadCursorA, (HINSTANCE)(uintptr_t)0x400000, (LPCVOID)(uintptr_t)1),
            "a cursor from the resources of module");
}
