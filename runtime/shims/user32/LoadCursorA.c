/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-loadcursora
 * RT-CASES:  predefined cursor of every IDC_ identifier, same handle on each call; unknown identifier fails with ERROR_RESOURCE_NAME_NOT_FOUND; success does not change last error
 * RT-TRAPS:  a cursor from the resources of module
 */
#include "rt/shim.h"
#include "rt/window.h"


SHIM(user32, HCURSOR, LoadCursorA, HINSTANCE hInstance, LPCVOID lpCursorName)
{
    uintptr_t id = (uintptr_t)lpCursorName;

    /* A cursor in a module's resources needs the resource loader, which arrives at Gate 4. */
    if (hInstance) RT_UNIMPLEMENTED(ctx, "a cursor from the resources of module %p (name %p): resource cursors arrive at Gate 4", (void *)hInstance, (void *)lpCursorName);

    /* The predefined cursors are the IDC_ identifiers of rt/win/user.h; any other name fails. */
    switch (id) {
    case IDC_ARROW: case IDC_IBEAM: case IDC_WAIT: case IDC_CROSS: case IDC_UPARROW:
    case IDC_SIZENWSE: case IDC_SIZENESW: case IDC_SIZEWE: case IDC_SIZENS: case IDC_SIZEALL:
    case IDC_NO: case IDC_HAND: case IDC_APPSTARTING: case IDC_HELP:
        return RT_HCURSOR_SYSTEM(id);
    default:
        return FAIL(ctx, ERROR_RESOURCE_NAME_NOT_FOUND, NULL);
    }
}
