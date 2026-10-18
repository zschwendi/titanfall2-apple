/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createeventa
 * RT-CASES:  an unnamed event starts set or not as bInitialState says; manual-reset stays set after a wait and auto-reset is reset by one; two unnamed creates give two events, each with ERROR_SUCCESS; a first create with a name gives ERROR_SUCCESS; a second create with the same name gives a handle to the same event with ERROR_ALREADY_EXISTS, and ignores its flags; names are case sensitive; a name of MAX_PATH characters is accepted; a name longer than MAX_PATH fails with ERROR_FILENAME_EXCED_RANGE; a name of another kind of object fails with ERROR_INVALID_HANDLE; the security attributes pointer, NULL or not, has no effect
 * RT-TRAPS:  an empty name; core: a backslash in the name
 */
#include "rt/shim.h"
#include "rt/sync.h"
#include "rt/locale.h"

SHIM(kernel32, HANDLE, CreateEventA, LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName)
{
    /* The core does the event (rt_event_create, rt/sync.h): it makes the event or opens the
     * named one, sets last error to ERROR_SUCCESS or ERROR_ALREADY_EXISTS, and fails with
     * ERROR_INVALID_HANDLE for a name of another kind of object.  This shim checks the name and
     * converts it.
     *
     * lpEventAttributes is not used, NULL or not.  Its security descriptor is not modelled, so
     * the event gets the default one; that is not a trap.  Its bInheritHandle has no effect,
     * because this runtime never creates a child process and so nothing can inherit the handle
     * (planner's decision, 2026-10-09).
     *
     * Names the core does not handle: the handle unit (core/handle.c) stops the run, with the
     * name in the report, for an empty name and for a backslash after the "Local" or "Global"
     * prefix.  The empty name is also refused here, as RT-TRAPS says.  A name longer than
     * MAX_PATH is refused here with ERROR_FILENAME_EXCED_RANGE, before the core sees it.
     * A backslash is not refused here: the lint forbids a shim from scanning a name for a
     * backslash, and the core already stops on it.  "Global" and "Local" prefixes are not refused here either; the core
     * decides them (see its UNSURE notes), and the planner has to look at that. */
    WCHAR wide[MAX_PATH + 1];
    int n;

    (void)lpEventAttributes;
    if (lpName == NULL) return rt_event_create(ctx, bManualReset, bInitialState, NULL);
    if (lpName[0] == 0) RT_UNIMPLEMENTED(ctx, "CreateEventA with an empty name");

    /* A name longer than MAX_PATH does not fit with its terminator: result 0. */
    n = rt_mb_to_wide(CP_ACP, 0, lpName, -1, wide, MAX_PATH + 1);
    if (n == 0) return FAIL(ctx, ERROR_FILENAME_EXCED_RANGE, NULL);
    return rt_event_create(ctx, bManualReset, bInitialState, wide);
}
