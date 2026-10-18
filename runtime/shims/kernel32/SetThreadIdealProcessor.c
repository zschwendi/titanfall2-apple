/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadidealprocessor
 * RT-CASES:  stores a processor below the machine's count and returns the previous one, 0 for a new thread; MAXIMUM_PROCESSORS returns the stored processor and changes nothing; a processor above MAXIMUM_PROCESSORS fails with ERROR_INVALID_PARAMETER; NULL fails with ERROR_INVALID_HANDLE; success leaves last error alone
 * RT-TRAPS:  core: a processor the machine does not report
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, DWORD, SetThreadIdealProcessor, HANDLE hThread, DWORD dwIdealProcessor)
{
    /* The core does all of it (rt_thread_set_ideal_processor, rt/thread.h): it records the
     * processor, returns the one recorded before, and fails with ERROR_INVALID_PARAMETER (above
     * MAXIMUM_PROCESSORS) or ERROR_INVALID_HANDLE (not an open handle, or not a thread), setting
     * last error itself.  Success leaves last error alone.  A processor from the machine's count
     * up to 63 stops the runtime in the core; the shim adds no guard for it.  Nothing is done to
     * the host thread: the value is a hint to a scheduler this runtime does not have. */
    return rt_thread_set_ideal_processor(hThread, dwIdealProcessor);
}
