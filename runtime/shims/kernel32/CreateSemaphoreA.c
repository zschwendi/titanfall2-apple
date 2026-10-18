/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createsemaphorew
 * RT-CASES:  an unnamed semaphore starts with its initial count: two waits succeed, the third times out; the security attributes are ignored, NULL or not; invalid counts fail with ERROR_INVALID_PARAMETER, also when the name exists; a named semaphore created twice is one object: ERROR_ALREADY_EXISTS, and the counts are ignored; the name is in the ANSI code page: byte 0xE9 is the wide name U+00E9; a name of MAX_PATH characters is accepted; a name longer than MAX_PATH fails with ERROR_FILENAME_EXCED_RANGE; a name held by an event fails with ERROR_INVALID_HANDLE; a new object sets last error to ERROR_SUCCESS
 * RT-TRAPS:  core: an empty name; core: a backslash in the name
 */
#include "rt/shim.h"
#include "rt/sync.h"
#include "rt/locale.h"

/* The `W` function is the same call with the name in UTF-16; this shim is its ANSI entry
 * point.  The name is converted with the ANSI code page first, so a name too long for the
 * buffer fails with ERROR_FILENAME_EXCED_RANGE even when the counts are also invalid.
 *
 * The security attributes are not used: this runtime has one process and no access checks,
 * so the descriptor has no effect, and bInheritHandle has no effect either because this
 * runtime never creates a child process (planner's decision, 2026-10-09). */
SHIM(kernel32, HANDLE, CreateSemaphoreA, LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCSTR lpName)
{
    WCHAR wide[MAX_PATH + 1];           /* MAX_PATH characters and the NUL: the documented limit */
    int n;

    (void)lpSemaphoreAttributes;
    if (!lpName) return rt_semaphore_create(ctx, lInitialCount, lMaximumCount, NULL);

    /* -1: the NUL is converted too, so the name must leave room for it in the MAX_PATH + 1 WCHARs
     * of the buffer. The only failure of this call with these arguments is a result that does not fit. */
    n = rt_mb_to_wide(CP_ACP, 0, lpName, -1, wide, MAX_PATH + 1);
    if (n == 0) return FAIL(ctx, ERROR_FILENAME_EXCED_RANGE, NULL);
    return rt_semaphore_create(ctx, lInitialCount, lMaximumCount, wide);
}
