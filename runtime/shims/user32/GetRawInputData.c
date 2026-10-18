/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getrawinputdata
 * RT-CASES:  NULL handle with RID_INPUT gives (UINT)-1 and ERROR_INVALID_HANDLE; any handle with RID_HEADER gives (UINT)-1 and ERROR_INVALID_HANDLE, buffer and size unchanged; with an input script a size query gives 0 and the size 48, RID_INPUT copies the 48 bytes of the packet and leaves the size alone, RID_HEADER copies 24, and the handle can be read again
 * RT-TRAPS:  GetRawInputData with pcbSize NULL; GetRawInputData with cbSizeHeader other than 24; GetRawInputData with uiCommand other than RID_INPUT or RID_HEADER; core: a handle this runtime did not issue; core: a handle whose packet is gone; core: a buffer that is too small
 */
#include "rt/shim.h"
#include "rt/rawinput.h"            /* HRAWINPUT, RID_INPUT, RID_HEADER, sizeof(RAWINPUTHEADER) is 24; the packets */

SHIM(user32, UINT, GetRawInputData, HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader)
{
    /* The order of these checks against the handle is not settled by Windows (MS; the stops are traps). */
    if (!pcbSize) RT_UNIMPLEMENTED(ctx, "GetRawInputData with pcbSize NULL");
    if (cbSizeHeader != 24) RT_UNIMPLEMENTED(ctx, "GetRawInputData with cbSizeHeader other than 24: got %u", cbSizeHeader);
    if (uiCommand != RID_INPUT && uiCommand != RID_HEADER)
        RT_UNIMPLEMENTED(ctx, "GetRawInputData with uiCommand other than RID_INPUT or RID_HEADER: 0x%x", uiCommand);
    /* With no input script no HRAWINPUT was ever given out (no device exists, no WM_INPUT is
     * posted), so every handle is invalid and nothing is written through pData or pcbSize. */
    if (!rt_raw_input_devices(NULL, 0)) return FAIL(ctx, ERROR_INVALID_HANDLE, (UINT)-1);
    /* With a script the handle is the lParam of a WM_INPUT this runtime posted, and the core
     * answers from that event's packet (rt_raw_input_data, rt/rawinput.h; contract C15 to C20).
     * A handle it did not issue or no longer has, and a buffer too small, are its stops. */
    return rt_raw_input_data(hRawInput, uiCommand, pData, pcbSize);
}
