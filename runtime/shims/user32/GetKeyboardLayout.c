/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getkeyboardlayout
 * RT-CASES:  idThread 0; idThread of the calling thread; does not change last error
 * RT-TRAPS:  keyboard layout of another thread
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HKL, GetKeyboardLayout, DWORD idThread)
{
    /* The machine has one keyboard layout, US English (RUNTIME.md 8.4). Another thread would have
     * to be looked up, which this runtime does not do: the run stops with the thread id. */
    if (idThread != 0 && idThread != RT_THREAD(ctx)->id)
        RT_UNIMPLEMENTED(ctx, "keyboard layout of another thread: %u", (unsigned)idThread);
    return RT_HKL_US;
}
