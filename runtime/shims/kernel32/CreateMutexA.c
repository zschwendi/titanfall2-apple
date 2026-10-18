/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createmutexa
 * RT-CASES:  an unnamed mutex created owned is owned by the caller and is released once; created unowned, a wait takes it and waits nest; a second create with the same name gives a handle to the same mutex with ERROR_ALREADY_EXISTS, and the second create does not own it; two unnamed creates give two mutexes, each with ERROR_SUCCESS; a name of another kind of object fails with ERROR_INVALID_HANDLE; a name of MAX_PATH characters is accepted; a name longer than MAX_PATH fails with ERROR_FILENAME_EXCED_RANGE; the security attributes pointer, NULL or not, has no effect
 * RT-TRAPS:  an empty name; core: a backslash in the name
 */
#include "rt/shim.h"
#include "rt/sync.h"
#include "rt/locale.h"

SHIM(kernel32, HANDLE, CreateMutexA, LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName)
{
    /* The core does the mutex (rt_mutex_create, rt/sync.h): it makes the mutex or opens the named
     * one, sets last error to ERROR_SUCCESS or ERROR_ALREADY_EXISTS, honours bInitialOwner only
     * when this call creates the mutex, and fails with ERROR_INVALID_HANDLE for a name of another
     * kind of object.  This shim checks the name and converts it.
     *
     * lpMutexAttributes is not used, NULL or not.  Its security descriptor is not modelled, so
     * the mutex gets the default one; that is not a trap.  Its bInheritHandle has no effect,
     * because this runtime never creates a child process and so nothing can inherit the handle
     * (planner's decision, 2026-10-09).
     *
     * The empty name is refused here, as RT-TRAPS says: the documentation says only NULL means
     * no name, and does not say what an empty name is.  The same is done in CreateEventA.
     * "Local" and "Global" prefixes are not refused here; the core decides them. */
    WCHAR wide[MAX_PATH + 1];
    int n;

    (void)lpMutexAttributes;
    if (lpName == NULL) return rt_mutex_create(ctx, bInitialOwner, NULL);
    if (lpName[0] == 0) RT_UNIMPLEMENTED(ctx, "CreateMutexA with an empty name");

    /* A name longer than MAX_PATH does not fit with its terminator: result 0. */
    n = rt_mb_to_wide(CP_ACP, 0, lpName, -1, wide, MAX_PATH + 1);
    if (n == 0) return FAIL(ctx, ERROR_FILENAME_EXCED_RANGE, NULL);
    return rt_mutex_create(ctx, bInitialOwner, wide);
}
