/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createsemaphoreexw
 * RT-CASES:  an unnamed semaphore starts with its initial count: two waits succeed, the third times out; the security attributes are ignored, NULL or not; the desired access is ignored: access 0 gives a working handle; invalid counts fail with ERROR_INVALID_PARAMETER, also when the name exists; a named semaphore created twice is one object: ERROR_ALREADY_EXISTS, and the counts are ignored; the name is UTF-16 as given: U+00E9 is the same object as the ANSI byte 0xE9; a name held by an event fails with ERROR_INVALID_HANDLE; a new object sets last error to ERROR_SUCCESS; the count works: rt_semaphore_release returns the previous count
 * RT-TRAPS:  dwFlags not 0; core: an empty name; core: a backslash in the name; core: a name longer than MAX_PATH
 */
#include "rt/shim.h"
#include "rt/sync.h"

/* The security attributes are not used: this runtime has one process and no access checks,
 * so the descriptor has no effect, and bInheritHandle has no effect either because this
 * runtime never creates a child process (planner's decision, 2026-10-09).
 *
 * dwDesiredAccess is not used: a handle of this runtime has no access rights of its own, so
 * the access mask asked for has nothing to be checked against.
 *
 * dwFlags is reserved and must be 0. Any other value is not implemented: the shim stops the
 * run with the value, and does not call the core.
 *
 * The core does the rest (rt_semaphore_create, rt/sync.h): the count checks, which come first
 * even when the name exists; the name space, where a name longer than MAX_PATH or an empty
 * name is not checked here but stops the run in the handle unit; and last error, which the
 * core sets on success as well (0, or ERROR_ALREADY_EXISTS for an existing name). */
SHIM(kernel32, HANDLE, CreateSemaphoreExW, LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCWSTR lpName, DWORD dwFlags, DWORD dwDesiredAccess)
{
    (void)lpSemaphoreAttributes;
    (void)dwDesiredAccess;
    if (dwFlags != 0) RT_UNIMPLEMENTED(ctx, "CreateSemaphoreExW with dwFlags not 0 (0x%x)", (unsigned)dwFlags);
    return rt_semaphore_create(ctx, lInitialCount, lMaximumCount, lpName);
}
