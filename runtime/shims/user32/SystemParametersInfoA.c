/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-systemparametersinfoa
 * RT-CASES:  SPI_GETWORKAREA copies the work area into the RECT and returns TRUE; does not change last error
 * RT-TRAPS:  an action other than SPI_GETWORKAREA with a RECT
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, SystemParametersInfoA, UINT uiAction, UINT uiParam, LPVOID pvParam, UINT fWinIni)
{
    /* The core answers SPI_GETWORKAREA (rt/window.h, rt_desktop): the work area of the one
     * monitor, which is the whole monitor, there being no taskbar.  Every other action, and a
     * NULL pvParam, stops with the action: a system parameter is never guessed. */
    (void)uiParam;     /* SPI_GETWORKAREA does not use it */
    (void)fWinIni;     /* SPI_GETWORKAREA does not use it */
    if (uiAction != SPI_GETWORKAREA || !pvParam)
        RT_UNIMPLEMENTED(ctx, "an action other than SPI_GETWORKAREA with a RECT: action 0x%x, pvParam %p",
                         uiAction, (void *)pvParam);
    *(RECT *)pvParam = rt_desktop()->work;
    return TRUE;
}
