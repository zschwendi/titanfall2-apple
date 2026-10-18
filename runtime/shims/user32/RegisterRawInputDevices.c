/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-registerrawinputdevices
 * RT-CASES:  (NULL, 0, 0) and a good list of two with size 0 fail with ERROR_INVALID_PARAMETER; two entries (page 1, usages 5 and 4, flags 0, no target) with size 16 succeed and leave last error alone, and two collections are registered; the same two with RIDEV_INPUTSINK and no target fail with ERROR_INVALID_PARAMETER and record nothing; the same two with RIDEV_INPUTSINK and a window as target succeed, and the record of usage 5 has the flag and the window; one entry (page 1, usage 2, the window as target) with size 16, the game's shape, succeeds
 * RT-TRAPS:  core: RIDEV_DEVNOTIFY; core: RIDEV_REMOVE with a target window
 */
#include "rt/shim.h"
#include "rt/rawinput.h"

SHIM(user32, BOOL, RegisterRawInputDevices, PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize)
{
    /* The core does all of it (rt_raw_input_register, rt/rawinput.h): it checks the size and the
     * RIDEV_INPUTSINK rule, records the registrations of the process, and fails with
     * ERROR_INVALID_PARAMETER, setting last error itself; success leaves last error alone.  Its
     * stops (RIDEV_DEVNOTIFY, RIDEV_REMOVE with a target, and the others its header lists) are
     * fatal in the core, so this shim adds no guard for them.  Nothing is posted to the window:
     * no input arrives in this runtime yet. */
    return rt_raw_input_register(pRawInputDevices, uiNumDevices, cbSize);
}
