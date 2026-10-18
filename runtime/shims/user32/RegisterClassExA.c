/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-registerclassexa
 * RT-CASES:  registers a class and returns its atom; a name registered again fails with ERROR_CLASS_ALREADY_EXISTS; a name in other ASCII case is taken too; a NULL hInstance is the main module; cbSize other than 80 fails with ERROR_INVALID_PARAMETER; negative extra byte counts fail with ERROR_INVALID_PARAMETER; a NULL structure or class name fails with ERROR_INVALID_PARAMETER; a class name of 255 characters registers; a class name of 257 characters fails with ERROR_INVALID_PARAMETER; a successful registration leaves last error alone
 * RT-TRAPS:  class name of 256 characters; core: class name is an atom; core: empty class name; core: NULL window procedure; core: menu name not NULL; core: cbClsExtra or cbWndExtra above 40; core: name registered with a different hInstance; core: more than 256 classes
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, ATOM, RegisterClassExA, const WNDCLASSEXA *lpwcx)
{
    /* A class name of exactly 256 characters: the documentation gives 256 as the maximum
     * length, and whether such a name registers is not settled (review S23).  An atom is not
     * a string (it is below 0x10000), so it is never read. */
    if (lpwcx && lpwcx->lpszClassName && (ULONG_PTR)lpwcx->lpszClassName >= 0x10000) {
        unsigned n = 0;
        while (n < 256 && lpwcx->lpszClassName[n]) n++;
        if (n == 256 && !lpwcx->lpszClassName[256])
            RT_UNIMPLEMENTED(ctx, "RegisterClassExA with a class name of 256 characters");
    }
    return rt_wndclass_register((const WNDCLASSEXW *)lpwcx, false);
}
