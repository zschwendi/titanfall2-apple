/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createsemaphorew
 * RT-CASES:  an unnamed semaphore starts with its initial count: two waits succeed, the third times out; the security attributes are ignored, NULL or not; invalid counts fail with ERROR_INVALID_PARAMETER, also when the name exists; a named semaphore created twice is one object: ERROR_ALREADY_EXISTS, and the counts are ignored; the name is UTF-16 as given: U+00E9 is the same object as the ANSI byte 0xE9; a name held by an event fails with ERROR_INVALID_HANDLE; a new object sets last error to ERROR_SUCCESS
 * RT-TRAPS:  core: an empty name; core: a backslash in the name; core: a name longer than MAX_PATH
 */
#include "rt/shim.h"
#include "rt/sync.h"

/* The security attributes are not used: this runtime has one process and no access checks,
 * so the descriptor has no effect, and bInheritHandle has no effect either because this
 * runtime never creates a child process (planner's decision, 2026-10-09).
 *
 * The core does the rest (rt_semaphore_create, rt/sync.h): the count checks, which come first
 * even when the name exists; the name space, where a name longer than MAX_PATH or an empty
 * name is not checked here but stops the run in the handle unit; and last error, which the
 * core sets on success as well (0, or ERROR_ALREADY_EXISTS for an existing name). */
SHIM(kernel32, HANDLE, CreateSemaphoreW, LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCWSTR lpName)
{
    (void)lpSemaphoreAttributes;
    return rt_semaphore_create(ctx, lInitialCount, lMaximumCount, lpName);
}
