/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-extescape
 * RT-CASES:  escape 8 (QUERYESCSUPPORT) returns 0 and leaves the buffers alone; application-defined escape 0x110000 returns 0 and leaves the buffers alone; NULL and non-NULL device context; does not change last error; logs its line once
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(gdi32, INT, ExtEscape, HDC hdc, INT iEscape, INT cjInput, LPCVOID lpInData, INT cjOutput, LPVOID lpOutData)
{
    /* This machine's display driver implements no escape, so every escape is answered with the
     * documented return value for an escape that is not implemented: 0. Nothing is read or
     * written, and last error is left alone. */
    (void)hdc;
    (void)cjInput;
    (void)lpInData;
    (void)cjOutput;
    (void)lpOutData;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "gdi32.dll!ExtEscape", "ExtEscape(%d): the display has no such feature", iEscape);
    return 0;
}
