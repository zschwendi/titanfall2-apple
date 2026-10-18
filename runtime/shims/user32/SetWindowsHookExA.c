/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowshookexa
 * RT-CASES:  low-level hooks on no thread return handles, with or without a module; keyboard and mouse hooks on the calling thread return handles, with or without a module; keyboard and mouse hooks on no thread return handles when they have a module; hooks that succeed return different handles; a filter type outside -1 to 14 fails with ERROR_INVALID_HOOK_FILTER; a NULL procedure fails with ERROR_INVALID_FILTER_PROC whatever the type; a low-level hook on a thread fails with ERROR_GLOBAL_ONLY_HOOK; a keyboard or mouse hook on no thread without a module fails with ERROR_HOOK_NEEDS_HMOD; success leaves last error alone
 * RT-TRAPS:  core: WH_KEYBOARD or WH_MOUSE on a thread that is neither 0 nor the calling thread
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HHOOK, SetWindowsHookExA, INT idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId)
{
    /* The core makes the four argument checks, sets the last error when it fails and leaves it
     * alone when it succeeds.  A hook on another thread stops in the core (RT-TRAPS). */
    return rt_hook_set(idHook, lpfn, hmod, dwThreadId);
}
