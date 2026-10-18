/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/imm/nf-imm-immisuimessagea
 * RT-CASES:  a message that is not an input-method message returns FALSE; does not change last error; one of the eight with a NULL window returns TRUE
 * RT-TRAPS:  an input-method message 0x%x for window %p
 */
#include "rt/shim.h"
#include "rt/win/user.h"

SHIM(imm32, BOOL, ImmIsUIMessageA, HWND hWndIME, UINT msg, WPARAM wParam, LPARAM lParam)
{
    /* No input method exists here, so nothing sends an input-method message to this process:
     * such a message to a window is a stop, and with a NULL window it gives TRUE. Any other
     * message is not one of them and gives FALSE.
     * The eight input-method messages include WM_IME_SYSTEM (0x287): Windows gives TRUE for it,
     * not FALSE (Wine imm32 tests, test_ImmIsUIMessage; review C7), so with a window it is a stop too. */
    (void)wParam;
    (void)lParam;
    switch (msg) {
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_SETCONTEXT:
    case WM_IME_NOTIFY:
    case WM_IME_COMPOSITIONFULL:
    case WM_IME_SELECT:
    case WM_IME_SYSTEM:
        if (!hWndIME) return TRUE;
        RT_UNIMPLEMENTED(ctx, "an input-method message 0x%x for window %p: none can exist without an input method", msg, (void *)hWndIME);
    default:
        return FALSE;
    }
}
