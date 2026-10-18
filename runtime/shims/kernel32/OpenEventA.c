/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-openeventa
 * RT-CASES:  opens an existing named event, and a set through one handle shows through the other; a name with a Local prefix names the same event as the name without it; the access mask and the inherit flag are ignored; a name of MAX_PATH characters is accepted; a name that does not exist fails with ERROR_FILE_NOT_FOUND; a NULL name fails with ERROR_INVALID_PARAMETER; a name that belongs to an object of another kind fails with ERROR_INVALID_HANDLE; a name longer than MAX_PATH fails with ERROR_FILENAME_EXCED_RANGE; does not change last error on success
 * RT-TRAPS:  an empty name; core: a backslash in the name
 */
#include "rt/shim.h"
#include "rt/sync.h"
#include "rt/locale.h"

SHIM(kernel32, HANDLE, OpenEventA, DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)
{
    /* The runtime has no access checks and no child processes, so the access mask and the
     * inherit flag have nothing to act on and are ignored. */
    WCHAR wide[MAX_PATH + 1];           /* MAX_PATH characters and the NUL: the documented limit */

    (void)dwDesiredAccess;
    (void)bInheritHandle;

    /* A NULL name goes to the core, which fails it with ERROR_INVALID_PARAMETER. */
    if (lpName == NULL)
        return rt_event_open(ctx, NULL);

    if (rt_mb_to_wide(CP_ACP, 0, lpName, -1, wide, MAX_PATH + 1) == 0)
        return FAIL(ctx, ERROR_FILENAME_EXCED_RANGE, NULL);

    /* The documentation does not say what Windows does with an empty name, so the shim refuses it.
     * The other name forms the core refuses (nothing after the Local or Global prefix, a backslash
     * after the prefix) are left to the core, which stops on them with the name in the report
     * (core/handle.c): this file is not allowed to compare a backslash. */
    if (wide[0] == 0)
        RT_UNIMPLEMENTED(ctx, "OpenEventA with an empty name: \"%s\"", lpName);

    return rt_event_open(ctx, wide);
}
