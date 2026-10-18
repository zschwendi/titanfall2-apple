/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread
 * RT-CASES:  a thread runs its start routine with the parameter, and the handle becomes signalled; lpThreadId receives a non-zero id, and may be NULL; success leaves last error alone; a CREATE_SUSPENDED thread does not run until ResumeThread, which returns 1; STACK_SIZE_PARAM_IS_A_RESERVATION is accepted and changes nothing; a stack above 1 GiB fails with ERROR_NOT_ENOUGH_MEMORY and creates nothing
 * RT-TRAPS:  core: creation flags other than CREATE_SUSPENDED and STACK_SIZE_PARAM_IS_A_RESERVATION; core: start address 0
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, HANDLE, CreateThread, LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId)
{
    /* The security attributes are ignored.  This runtime checks no access to a thread handle,
     * and it starts no child processes, so nothing can inherit the handle. */
    (void)lpThreadAttributes;

    /* The core does all of it (rt_thread_create, rt/thread.h): the stack rule, the new thread
     * and its object, the start, and the failure with ERROR_NOT_ENOUGH_MEMORY (set by the core)
     * when the stack is above 1 GiB.  It stops (fatal) on the creation flags it does not handle
     * and on a start address of 0, as RT-TRAPS says.  Last error is left alone on success. */
    return rt_thread_create(ctx, dwStackSize, lpStartAddress, (uint64_t)(uintptr_t)lpParameter, dwCreationFlags, lpThreadId);
}
