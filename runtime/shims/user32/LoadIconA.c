/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-loadicona
 * RT-CASES:  predefined icon of every IDI_ identifier, same handle on each call; unknown identifier fails with ERROR_RESOURCE_NAME_NOT_FOUND; an icon of the executable is not there: NULL with ERROR_RESOURCE_NAME_NOT_FOUND and one WARN line; success does not change last error
 * RT-TRAPS:  an icon from the resources of module
 */
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/module.h"

SHIM(user32, HICON, LoadIconA, HINSTANCE hInstance, LPCVOID lpIconName)
{
    uintptr_t id = (uintptr_t)lpIconName;

    if (hInstance) {
        /* An icon of a module's resources.  The executable is our own program, which has no
         * resources, and the original's executable is never read: its icons are not there, and
         * the answer is the one for a missing resource (the contract's decision; the caller
         * seen, a window class's icon field, takes NULL).  Wine's kernel32/tests/resource.c
         * asserts 1814 from FindResource for a name its module lacks (confirmed by
         * docs/reviews/contracts-check-2.md, Q1); that LoadIcon reports the lookup's code is
         * from memory (Q2 there: not settled, no test loads a missing icon).  Any other
         * module's resources are not read: a stop. */
        if (hInstance != (HINSTANCE)(uintptr_t)rt_module_exe()->base)
            RT_UNIMPLEMENTED(ctx, "an icon from the resources of module %p (name %p): only the executable's are answered", (void *)hInstance, (void *)lpIconName);
        if (id < 0x10000u)
            rt_log(RT_CH_WINDOW, RT_LL_WARN, "LoadIconA: icon %u of the executable (module %p) is not there: this program has no resources and the original's are never read; NULL, last error 1814", (unsigned)id, (void *)hInstance);
        else
            rt_log(RT_CH_WINDOW, RT_LL_WARN, "LoadIconA: icon \"%.64s\" of the executable (module %p) is not there: this program has no resources and the original's are never read; NULL, last error 1814", (const char *)lpIconName, (void *)hInstance);
        return FAIL(ctx, ERROR_RESOURCE_NAME_NOT_FOUND, NULL);
    }

    /* The predefined icons are the IDI_ identifiers of rt/win/user.h; any other name fails. */
    switch (id) {
    case IDI_APPLICATION: case IDI_HAND: case IDI_QUESTION: case IDI_EXCLAMATION:
    case IDI_ASTERISK: case IDI_WINLOGO: case IDI_SHIELD:
        return RT_HICON_SYSTEM(id);
    default:
        return FAIL(ctx, ERROR_RESOURCE_NAME_NOT_FOUND, NULL);
    }
}
